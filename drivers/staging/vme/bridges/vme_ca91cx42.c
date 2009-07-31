/*
 * Support for the Tundra Universe I/II VME-PCI Bridge Chips
 *
 * Author: Martyn Welch <martyn.welch@gefanuc.com>
 * Copyright 2008 GE Fanuc Intelligent Platforms Embedded Systems, Inc.
 *
 * Based on work by Tom Armistead and Ajit Prem
 * Copyright 2004 Motorola Inc.
 *
 * Derived from ca91c042.c by Michael Wyrick
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/version.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/proc_fs.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/poll.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <asm/time.h>
#include <asm/io.h>
#include <asm/uaccess.h>

#include "../vme.h"
#include "../vme_bridge.h"
#include "vme_ca91cx42.h"

extern struct vmeSharedData *vmechip_interboard_data;
extern dma_addr_t vmechip_interboard_datap;
extern const int vmechip_revision;
extern const int vmechip_devid;
extern const int vmechip_irq;
extern int vmechip_irq_overhead_ticks;
extern char *vmechip_baseaddr;
extern const int vme_slotnum;
extern int vme_syscon;
extern unsigned int out_image_va[];
extern unsigned int vme_irqlog[8][0x100];

static int outCTL[] = { LSI0_CTL, LSI1_CTL, LSI2_CTL, LSI3_CTL,
	LSI4_CTL, LSI5_CTL, LSI6_CTL, LSI7_CTL
};

static int outBS[] = { LSI0_BS, LSI1_BS, LSI2_BS, LSI3_BS,
	LSI4_BS, LSI5_BS, LSI6_BS, LSI7_BS
};

static int outBD[] = { LSI0_BD, LSI1_BD, LSI2_BD, LSI3_BD,
	LSI4_BD, LSI5_BD, LSI6_BD, LSI7_BD
};

static int outTO[] = { LSI0_TO, LSI1_TO, LSI2_TO, LSI3_TO,
	LSI4_TO, LSI5_TO, LSI6_TO, LSI7_TO
};

static int inCTL[] = { VSI0_CTL, VSI1_CTL, VSI2_CTL, VSI3_CTL,
	VSI4_CTL, VSI5_CTL, VSI6_CTL, VSI7_CTL
};

static int inBS[] = { VSI0_BS, VSI1_BS, VSI2_BS, VSI3_BS,
	VSI4_BS, VSI5_BS, VSI6_BS, VSI7_BS
};

static int inBD[] = { VSI0_BD, VSI1_BD, VSI2_BD, VSI3_BD,
	VSI4_BD, VSI5_BD, VSI6_BD, VSI7_BD
};

static int inTO[] = { VSI0_TO, VSI1_TO, VSI2_TO, VSI3_TO,
	VSI4_TO, VSI5_TO, VSI6_TO, VSI7_TO
};
static int vmevec[7] = { V1_STATID, V2_STATID, V3_STATID, V4_STATID,
	V5_STATID, V6_STATID, V7_STATID
};

struct interrupt_counters {
	unsigned int acfail;
	unsigned int sysfail;
	unsigned int sw_int;
	unsigned int sw_iack;
	unsigned int verr;
	unsigned int lerr;
	unsigned int lm;
	unsigned int mbox;
	unsigned int dma;
	unsigned int virq[7];
	unsigned int vown;
};

extern wait_queue_head_t dma_queue[];
extern wait_queue_head_t lm_queue;
extern wait_queue_head_t mbox_queue;

extern int tb_speed;

unsigned int uni_irq_time;
unsigned int uni_dma_irq_time;
unsigned int uni_lm_event;

static spinlock_t lm_lock = SPIN_LOCK_UNLOCKED;

static struct interrupt_counters Interrupt_counters = { 0, 0,
	0, 0, 0, 0,
	0, 0, 0,
	{0, 0, 0, 0, 0, 0, 0},
	0
};

#define read_register(offset) readl(vmechip_baseaddr + offset)
#define write_register(value,offset) writel(value, vmechip_baseaddr + offset)
#define read_register_word(offset) readw(vmechip_baseaddr + offset)
#define write_register_word(value,offset) writew(value, vmechip_baseaddr + offset)

int uni_procinfo(char *buf)
{
	char *p;

	p = buf;

	p += sprintf(p, "\n");
	{
		unsigned long misc_ctl;

		misc_ctl = read_register(MISC_CTL);
		p += sprintf(p, "MISC_CTL:\t\t\t0x%08lx\n", misc_ctl);
		p += sprintf(p, "VME Bus Time Out:\t\t");
		switch ((misc_ctl & UNIV_BM_MISC_CTL_VBTO) >>
			UNIV_OF_MISC_CTL_VBTO) {
		case 0x0:
			p += sprintf(p, "Disabled\n");
			break;
		case 0x1:
			p += sprintf(p, "16 us\n");
			break;
		case 0x2:
			p += sprintf(p, "32 us\n");
			break;
		case 0x3:
			p += sprintf(p, "64 us\n");
			break;
		case 0x4:
			p += sprintf(p, "128 us\n");
			break;
		case 0x5:
			p += sprintf(p, "256 us\n");
			break;
		case 0x6:
			p += sprintf(p, "512 us\n");
			break;
		case 0x7:
			p += sprintf(p, "1024 us\n");
			break;
		default:
			p += sprintf(p, "Reserved Value, Undefined\n");
		}
		p += sprintf(p, "VME Arbitration Time Out:\t");
		switch ((misc_ctl & UNIV_BM_MISC_CTL_VARBTO) >>
			UNIV_OF_MISC_CTL_VARBTO) {
		case 0x0:
			p += sprintf(p, "Disabled");
			break;
		case 0x1:
			p += sprintf(p, "16 us");
			break;
		case 0x2:
			p += sprintf(p, "256 us");
			break;
		default:
			p += sprintf(p, "Reserved Value, Undefined");
		}
		if (misc_ctl & UNIV_BM_MISC_CTL_VARB)
			p += sprintf(p, ", Priority Arbitration\n");
		else
			p += sprintf(p, ", Round Robin Arbitration\n");
		p += sprintf(p, "\n");
	}

	{
		unsigned int lmisc;
		unsigned int crt;
		unsigned int cwt;

		lmisc = read_register(LMISC);
		p += sprintf(p, "LMISC:\t\t\t\t0x%08x\n", lmisc);
		crt = (lmisc & UNIV_BM_LMISC_CRT) >> UNIV_OF_LMISC_CRT;
		cwt = (lmisc & UNIV_BM_LMISC_CWT) >> UNIV_OF_LMISC_CWT;
		p += sprintf(p, "Coupled Request Timer:\t\t");
		switch (crt) {
		case 0x0:
			p += sprintf(p, "Disabled\n");
			break;
		case 0x1:
			p += sprintf(p, "128 us\n");
			break;
		case 0x2:
			p += sprintf(p, "256 us\n");
			break;
		case 0x3:
			p += sprintf(p, "512 us\n");
			break;
		case 0x4:
			p += sprintf(p, "1024 us\n");
			break;
		case 0x5:
			p += sprintf(p, "2048 us\n");
			break;
		case 0x6:
			p += sprintf(p, "4096 us\n");
			break;
		default:
			p += sprintf(p, "Reserved\n");
		}
		p += sprintf(p, "Coupled Window Timer:\t\t");
		switch (cwt) {
		case 0x0:
			p += sprintf(p, "Disabled\n");
			break;
		case 0x1:
			p += sprintf(p, "16 PCI Clocks\n");
			break;
		case 0x2:
			p += sprintf(p, "32 PCI Clocks\n");
			break;
		case 0x3:
			p += sprintf(p, "64 PCI Clocks\n");
			break;
		case 0x4:
			p += sprintf(p, "128 PCI Clocks\n");
			break;
		case 0x5:
			p += sprintf(p, "256 PCI Clocks\n");
			break;
		case 0x6:
			p += sprintf(p, "512 PCI Clocks\n");
			break;
		default:
			p += sprintf(p, "Reserved\n");
		}
		p += sprintf(p, "\n");
	}
	{
		unsigned int mast_ctl;

		mast_ctl = read_register(MAST_CTL);
		p += sprintf(p, "MAST_CTL:\t\t\t0x%08x\n", mast_ctl);
		{
			int retries;

			retries = ((mast_ctl & UNIV_BM_MAST_CTL_MAXRTRY)
				   >> UNIV_OF_MAST_CTL_MAXRTRY) * 64;
			p += sprintf(p, "Max PCI Master Retries:\t\t");
			if (retries)
				p += sprintf(p, "%d\n", retries);
			else
				p += sprintf(p, "Forever\n");
		}

		p += sprintf(p, "Posted Write Transfer Count:\t");
		switch ((mast_ctl & UNIV_BM_MAST_CTL_PWON) >>
			UNIV_OF_MAST_CTL_PWON) {
		case 0x0:
			p += sprintf(p, "128 Bytes\n");
			break;
		case 0x1:
			p += sprintf(p, "256 Bytes\n");
			break;
		case 0x2:
			p += sprintf(p, "512 Bytes\n");
			break;
		case 0x3:
			p += sprintf(p, "1024 Bytes\n");
			break;
		case 0x4:
			p += sprintf(p, "2048 Bytes\n");
			break;
		case 0x5:
			p += sprintf(p, "4096 Bytes\n");
			break;
		default:
			p += sprintf(p, "Undefined\n");
		}

		p += sprintf(p, "VMEbus Request Level:\t\t");
		switch ((mast_ctl & UNIV_BM_MAST_CTL_VRL) >>
			UNIV_OF_MAST_CTL_VRL) {
		case 0x0:
			p += sprintf(p, "Level 0\n");
		case 0x1:
			p += sprintf(p, "Level 1\n");
		case 0x2:
			p += sprintf(p, "Level 2\n");
		case 0x3:
			p += sprintf(p, "Level 3\n");
		}
		p += sprintf(p, "VMEbus Request Mode:\t\t");
		if (mast_ctl & UNIV_BM_MAST_CTL_VRM)
			p += sprintf(p, "Fair Request Mode\n");
		else
			p += sprintf(p, "Demand Request Mode\n");
		p += sprintf(p, "VMEbus Release Mode:\t\t");
		if (mast_ctl & UNIV_BM_MAST_CTL_VREL)
			p += sprintf(p, "Release on Request\n");
		else
			p += sprintf(p, "Release when Done\n");
		p += sprintf(p, "VMEbus Ownership Bit:\t\t");
		if (mast_ctl & UNIV_BM_MAST_CTL_VOWN)
			p += sprintf(p, "Acquire and hold VMEbus\n");
		else
			p += sprintf(p, "Release VMEbus\n");
		p += sprintf(p, "VMEbus Ownership Bit Ack:\t");
		if (mast_ctl & UNIV_BM_MAST_CTL_VOWN_ACK)
			p += sprintf(p, "Owning VMEbus\n");
		else
			p += sprintf(p, "Not Owning VMEbus\n");
		p += sprintf(p, "\n");
	}
	{
		unsigned int misc_stat;

		misc_stat = read_register(MISC_STAT);
		p += sprintf(p, "MISC_STAT:\t\t\t0x%08x\n", misc_stat);
		p += sprintf(p, "Universe BBSY:\t\t\t");
		if (misc_stat & UNIV_BM_MISC_STAT_MYBBSY)
			p += sprintf(p, "Negated\n");
		else
			p += sprintf(p, "Asserted\n");
		p += sprintf(p, "Transmit FIFO:\t\t\t");
		if (misc_stat & UNIV_BM_MISC_STAT_TXFE)
			p += sprintf(p, "Empty\n");
		else
			p += sprintf(p, "Not empty\n");
		p += sprintf(p, "Receive FIFO:\t\t\t");
		if (misc_stat & UNIV_BM_MISC_STAT_RXFE)
			p += sprintf(p, "Empty\n");
		else
			p += sprintf(p, "Not Empty\n");
		p += sprintf(p, "\n");
	}

	p += sprintf(p, "Latency Timer:\t\t\t%02d Clocks\n\n",
		     (read_register(UNIV_PCI_MISC0) &
		      UNIV_BM_PCI_MISC0_LTIMER) >> UNIV_OF_PCI_MISC0_LTIMER);

	{
		unsigned int lint_en;
		unsigned int lint_stat;

		lint_en = read_register(LINT_EN);
		lint_stat = read_register(LINT_STAT);

#define REPORT_IRQ(name,field)     \
    p += sprintf(p, (lint_en & UNIV_BM_LINT_##name) ? "Enabled" : "Masked"); \
    p += sprintf(p, ", triggered %d times", Interrupt_counters.field); \
    p += sprintf(p, (lint_stat & UNIV_BM_LINT_##name) ? ", irq now active\n" : "\n");
		p += sprintf(p, "ACFAIL Interrupt:\t\t");
		REPORT_IRQ(ACFAIL, acfail);
		p += sprintf(p, "SYSFAIL Interrupt:\t\t");
		REPORT_IRQ(SYSFAIL, sysfail);
		p += sprintf(p, "SW_INT Interrupt:\t\t");
		REPORT_IRQ(SW_INT, sw_int);
		p += sprintf(p, "SW_IACK Interrupt:\t\t");
		REPORT_IRQ(SW_IACK, sw_iack);
		p += sprintf(p, "VERR Interrupt:\t\t\t");
		REPORT_IRQ(VERR, verr);
		p += sprintf(p, "LERR Interrupt:\t\t\t");
		REPORT_IRQ(LERR, lerr);
		p += sprintf(p, "LM Interrupt:\t\t\t");
		REPORT_IRQ(LM, lm);
		p += sprintf(p, "MBOX Interrupt:\t\t\t");
		REPORT_IRQ(MBOX, mbox);
		p += sprintf(p, "DMA Interrupt:\t\t\t");
		REPORT_IRQ(DMA, dma);
		p += sprintf(p, "VIRQ7 Interrupt:\t\t");
		REPORT_IRQ(VIRQ7, virq[7 - 1]);
		p += sprintf(p, "VIRQ6 Interrupt:\t\t");
		REPORT_IRQ(VIRQ6, virq[6 - 1]);
		p += sprintf(p, "VIRQ5 Interrupt:\t\t");
		REPORT_IRQ(VIRQ5, virq[5 - 1]);
		p += sprintf(p, "VIRQ4 Interrupt:\t\t");
		REPORT_IRQ(VIRQ4, virq[4 - 1]);
		p += sprintf(p, "VIRQ3 Interrupt:\t\t");
		REPORT_IRQ(VIRQ3, virq[3 - 1]);
		p += sprintf(p, "VIRQ2 Interrupt:\t\t");
		REPORT_IRQ(VIRQ2, virq[2 - 1]);
		p += sprintf(p, "VIRQ1 Interrupt:\t\t");
		REPORT_IRQ(VIRQ1, virq[1 - 1]);
		p += sprintf(p, "VOWN Interrupt:\t\t\t");
		REPORT_IRQ(VOWN, vown);
		p += sprintf(p, "\n");
#undef REPORT_IRQ
	}
	{
		unsigned long vrai_ctl;

		vrai_ctl = read_register(VRAI_CTL);
		if (vrai_ctl & UNIV_BM_VRAI_CTL_EN) {
			unsigned int vrai_bs;

			vrai_bs = read_register(VRAI_BS);
			p += sprintf(p,
				     "VME Register Image:\t\tEnabled at VME-Address 0x%x\n",
				     vrai_bs);
		} else
			p += sprintf(p, "VME Register Image:\t\tDisabled\n");
	}
	{
		unsigned int slsi;

		slsi = read_register(SLSI);
		if (slsi & UNIV_BM_SLSI_EN) {
			/* Not implemented */
		} else {
			p += sprintf(p, "Special PCI Slave Image:\tDisabled\n");
		}
	}
	{
		int i;

		for (i = 0; i < (vmechip_revision > 0 ? 8 : 4); i++) {
			unsigned int ctl, bs, bd, to, vstart, vend;

			ctl = readl(vmechip_baseaddr + outCTL[i]);
			bs = readl(vmechip_baseaddr + outBS[i]);
			bd = readl(vmechip_baseaddr + outBD[i]);
			to = readl(vmechip_baseaddr + outTO[i]);

			vstart = bs + to;
			vend = bd + to;

			p += sprintf(p, "PCI Slave Image %d:\t\t", i);
			if (ctl & UNIV_BM_LSI_CTL_EN) {
				p += sprintf(p, "Enabled");
				if (ctl & UNIV_BM_LSI_CTL_PWEN)
					p += sprintf(p,
						     ", Posted Write Enabled\n");
				else
					p += sprintf(p, "\n");
				p += sprintf(p,
					     "\t\t\t\tPCI Addresses from 0x%x to 0x%x\n",
					     bs, bd);
				p += sprintf(p,
					     "\t\t\t\tVME Addresses from 0x%x to 0x%x\n",
					     vstart, vend);
			} else
				p += sprintf(p, "Disabled\n");
		}
		p += sprintf(p, "\n");
	}
	{
		int i;
		for (i = 0; i < (vmechip_revision > 0 ? 8 : 4); i++) {
			unsigned int ctl, bs, bd, to, vstart, vend;

			ctl = readl(vmechip_baseaddr + inCTL[i]);
			bs = readl(vmechip_baseaddr + inBS[i]);
			bd = readl(vmechip_baseaddr + inBD[i]);
			to = readl(vmechip_baseaddr + inTO[i]);
			vstart = bs + to;
			vend = bd + to;
			p += sprintf(p, "VME Slave Image %d:\t\t", i);
			if (ctl & UNIV_BM_LSI_CTL_EN) {
				p += sprintf(p, "Enabled");
				if (ctl & UNIV_BM_LSI_CTL_PWEN)
					p += sprintf(p,
						     ", Posted Write Enabled\n");
				else
					p += sprintf(p, "\n");
				p += sprintf(p,
					     "\t\t\t\tVME Addresses from 0x%x to 0x%x\n",
					     bs, bd);
				p += sprintf(p,
					     "\t\t\t\tPCI Addresses from 0x%x to 0x%x\n",
					     vstart, vend);
			} else
				p += sprintf(p, "Disabled\n");
		}
	}

	return p - buf;
}

//----------------------------------------------------------------------------
//  uni_bus_error_chk()
//----------------------------------------------------------------------------
int uni_bus_error_chk(int clrflag)
{
	int tmp;
	tmp = readl(vmechip_baseaddr + PCI_COMMAND);
	if (tmp & 0x08000000) {	// S_TA is Set
		if (clrflag)
			writel(tmp | 0x08000000,
			       vmechip_baseaddr + PCI_COMMAND);
		return (1);
	}
	return (0);
}

//-----------------------------------------------------------------------------
// Function   : DMA_uni_irqhandler
// Inputs     : void
// Outputs    : void
// Description: Saves DMA completion timestamp and then wakes up DMA queue
//-----------------------------------------------------------------------------
static void DMA_uni_irqhandler(void)
{
	uni_dma_irq_time = uni_irq_time;
	wake_up(&dma_queue[0]);
}

//-----------------------------------------------------------------------------
// Function   : LERR_uni_irqhandler
// Inputs     : void
// Outputs    : void
// Description:
//-----------------------------------------------------------------------------
static void LERR_uni_irqhandler(void)
{
	int val;

	val = readl(vmechip_baseaddr + DGCS);

	if (!(val & 0x00000800)) {
		printk(KERN_ERR
		       "ca91c042: LERR_uni_irqhandler DMA Read Error DGCS=%08X\n",
		       val);

	}
}

//-----------------------------------------------------------------------------
// Function   : VERR_uni_irqhandler
// Inputs     : void
// Outputs    : void
// Description:
//-----------------------------------------------------------------------------
static void VERR_uni_irqhandler(void)
{
	int val;

	val = readl(vmechip_baseaddr + DGCS);

	if (!(val & 0x00000800)) {
		printk(KERN_ERR
		       "ca91c042: VERR_uni_irqhandler DMA Read Error DGCS=%08X\n",
		       val);
	}

}

//-----------------------------------------------------------------------------
// Function   : MB_uni_irqhandler
// Inputs     : void
// Outputs    : void
// Description:
//-----------------------------------------------------------------------------
static void MB_uni_irqhandler(int mbox_mask)
{
	if (vmechip_irq_overhead_ticks != 0) {
		wake_up(&mbox_queue);
	}
}

//-----------------------------------------------------------------------------
// Function   : LM_uni_irqhandler
// Inputs     : void
// Outputs    : void
// Description:
//-----------------------------------------------------------------------------
static void LM_uni_irqhandler(int lm_mask)
{
	uni_lm_event = lm_mask;
	wake_up(&lm_queue);
}

//-----------------------------------------------------------------------------
// Function   : VIRQ_uni_irqhandler
// Inputs     : void
// Outputs    : void
// Description:
//-----------------------------------------------------------------------------
static void VIRQ_uni_irqhandler(int virq_mask)
{
	int iackvec, i;

	for (i = 7; i > 0; i--) {
		if (virq_mask & (1 << i)) {
			Interrupt_counters.virq[i - 1]++;
			iackvec = readl(vmechip_baseaddr + vmevec[i - 1]);
			vme_irqlog[i][iackvec]++;
		}
	}
}

//-----------------------------------------------------------------------------
// Function   : uni_irqhandler
// Inputs     : int irq, void *dev_id, struct pt_regs *regs
// Outputs    : void
// Description:
//-----------------------------------------------------------------------------
static irqreturn_t uni_irqhandler(int irq, void *dev_id)
{
	long stat, enable;

	if (dev_id != vmechip_baseaddr)
		return IRQ_NONE;

	uni_irq_time = get_tbl();

	stat = readl(vmechip_baseaddr + LINT_STAT);
	writel(stat, vmechip_baseaddr + LINT_STAT);	// Clear all pending ints
	enable = readl(vmechip_baseaddr + LINT_EN);
	stat = stat & enable;
	if (stat & 0x0100) {
		Interrupt_counters.dma++;
		DMA_uni_irqhandler();
	}
	if (stat & 0x0200) {
		Interrupt_counters.lerr++;
		LERR_uni_irqhandler();
	}
	if (stat & 0x0400) {
		Interrupt_counters.verr++;
		VERR_uni_irqhandler();
	}
	if (stat & 0xF0000) {
		Interrupt_counters.mbox++;
		MB_uni_irqhandler((stat & 0xF0000) >> 16);
	}
	if (stat & 0xF00000) {
		Interrupt_counters.lm++;
		LM_uni_irqhandler((stat & 0xF00000) >> 20);
	}
	if (stat & 0x0000FE) {
		VIRQ_uni_irqhandler(stat & 0x0000FE);
	}
	if (stat & UNIV_BM_LINT_ACFAIL) {
		Interrupt_counters.acfail++;
	}
	if (stat & UNIV_BM_LINT_SYSFAIL) {
		Interrupt_counters.sysfail++;
	}
	if (stat & UNIV_BM_LINT_SW_INT) {
		Interrupt_counters.sw_int++;
	}
	if (stat & UNIV_BM_LINT_SW_IACK) {
		Interrupt_counters.sw_iack++;
	}
	if (stat & UNIV_BM_LINT_VOWN) {
		Interrupt_counters.vown++;
	}

	return IRQ_HANDLED;
}

//-----------------------------------------------------------------------------
// Function   : uni_generate_irq
// Description:
//-----------------------------------------------------------------------------
int uni_generate_irq(virqInfo_t * vmeIrq)
{
	int timeout;
	int looptimeout;

	timeout = vmeIrq->waitTime;
	if (timeout == 0) {
		timeout++;	// Wait at least 1 tick...
	}
	looptimeout = HZ / 20;	// try for 1/20 second

	vmeIrq->timeOutFlag = 0;

	// Validate & setup vector register.
	if (vmeIrq->vector & 1) {	// Universe can only generate even vectors
		return (-EINVAL);
	}
	writel(vmeIrq->vector << 24, vmechip_baseaddr + STATID);

	// Assert VMEbus IRQ
	writel(1 << (vmeIrq->level + 24), vmechip_baseaddr + VINT_EN);

	// Wait for syscon to do iack
	while (readl(vmechip_baseaddr + VINT_STAT) &
	       (1 << (vmeIrq->level + 24))) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(looptimeout);
		timeout = timeout - looptimeout;
		if (timeout <= 0) {
			vmeIrq->timeOutFlag = 1;
			break;
		}
	}

	// Clear VMEbus IRQ bit
	writel(0, vmechip_baseaddr + VINT_EN);

	return (0);
}

//-----------------------------------------------------------------------------
// Function   : uni_set_arbiter
// Description:
//-----------------------------------------------------------------------------
int uni_set_arbiter(vmeArbiterCfg_t * vmeArb)
{
	int temp_ctl = 0;
	int vbto = 0;

	temp_ctl = readl(vmechip_baseaddr + MISC_CTL);
	temp_ctl &= 0x00FFFFFF;

	if (vmeArb->globalTimeoutTimer == 0xFFFFFFFF) {
		vbto = 7;
	} else if (vmeArb->globalTimeoutTimer > 1024) {
		return (-EINVAL);
	} else if (vmeArb->globalTimeoutTimer == 0) {
		vbto = 0;
	} else {
		vbto = 1;
		while ((16 * (1 << (vbto - 1))) < vmeArb->globalTimeoutTimer) {
			vbto += 1;
		}
	}
	temp_ctl |= (vbto << 28);

	if (vmeArb->arbiterMode == VME_PRIORITY_MODE) {
		temp_ctl |= 1 << 26;
	}

	if (vmeArb->arbiterTimeoutFlag) {
		temp_ctl |= 2 << 24;
	}

	writel(temp_ctl, vmechip_baseaddr + MISC_CTL);
	return (0);
}

//-----------------------------------------------------------------------------
// Function   : uni_get_arbiter
// Description:
//-----------------------------------------------------------------------------
int uni_get_arbiter(vmeArbiterCfg_t * vmeArb)
{
	int temp_ctl = 0;
	int vbto = 0;

	temp_ctl = readl(vmechip_baseaddr + MISC_CTL);

	vbto = (temp_ctl >> 28) & 0xF;
	if (vbto != 0) {
		vmeArb->globalTimeoutTimer = (16 * (1 << (vbto - 1)));
	}

	if (temp_ctl & (1 << 26)) {
		vmeArb->arbiterMode = VME_PRIORITY_MODE;
	} else {
		vmeArb->arbiterMode = VME_R_ROBIN_MODE;
	}

	if (temp_ctl & (3 << 24)) {
		vmeArb->arbiterTimeoutFlag = 1;
	}
	return (0);
}

//-----------------------------------------------------------------------------
// Function   : uni_set_requestor
// Description:
//-----------------------------------------------------------------------------
int uni_set_requestor(vmeRequesterCfg_t * vmeReq)
{
	int temp_ctl = 0;

	temp_ctl = readl(vmechip_baseaddr + MAST_CTL);
	temp_ctl &= 0xFF0FFFFF;

	if (vmeReq->releaseMode == 1) {
		temp_ctl |= (1 << 20);
	}

	if (vmeReq->fairMode == 1) {
		temp_ctl |= (1 << 21);
	}

	temp_ctl |= (vmeReq->requestLevel << 22);

	writel(temp_ctl, vmechip_baseaddr + MAST_CTL);
	return (0);
}

//-----------------------------------------------------------------------------
// Function   : uni_get_requestor
// Description:
//-----------------------------------------------------------------------------
int uni_get_requestor(vmeRequesterCfg_t * vmeReq)
{
	int temp_ctl = 0;

	temp_ctl = readl(vmechip_baseaddr + MAST_CTL);

	if (temp_ctl & (1 << 20)) {
		vmeReq->releaseMode = 1;
	}

	if (temp_ctl & (1 << 21)) {
		vmeReq->fairMode = 1;
	}

	vmeReq->requestLevel = (temp_ctl & 0xC00000) >> 22;

	return (0);
}

//-----------------------------------------------------------------------------
// Function   : uni_set_in_bound
// Description:
//-----------------------------------------------------------------------------
int uni_set_in_bound(vmeInWindowCfg_t * vmeIn)
{
	int temp_ctl = 0;

	// Verify input data
	if (vmeIn->windowNbr > 7) {
		return (-EINVAL);
	}
	if ((vmeIn->vmeAddrU) || (vmeIn->windowSizeU) || (vmeIn->pciAddrU)) {
		return (-EINVAL);
	}
	if ((vmeIn->vmeAddrL & 0xFFF) ||
	    (vmeIn->windowSizeL & 0xFFF) || (vmeIn->pciAddrL & 0xFFF)) {
		return (-EINVAL);
	}

	if (vmeIn->bcastRespond2esst) {
		return (-EINVAL);
	}
	switch (vmeIn->addrSpace) {
	case VME_A64:
	case VME_CRCSR:
	case VME_USER3:
	case VME_USER4:
		return (-EINVAL);
	case VME_A16:
		temp_ctl |= 0x00000;
		break;
	case VME_A24:
		temp_ctl |= 0x10000;
		break;
	case VME_A32:
		temp_ctl |= 0x20000;
		break;
	case VME_USER1:
		temp_ctl |= 0x60000;
		break;
	case VME_USER2:
		temp_ctl |= 0x70000;
		break;
	}

	// Disable while we are mucking around
	writel(0x00000000, vmechip_baseaddr + inCTL[vmeIn->windowNbr]);
	writel(vmeIn->vmeAddrL, vmechip_baseaddr + inBS[vmeIn->windowNbr]);
	writel(vmeIn->vmeAddrL + vmeIn->windowSizeL,
	       vmechip_baseaddr + inBD[vmeIn->windowNbr]);
	writel(vmeIn->pciAddrL - vmeIn->vmeAddrL,
	       vmechip_baseaddr + inTO[vmeIn->windowNbr]);

	// Setup CTL register.
	if (vmeIn->wrPostEnable)
		temp_ctl |= 0x40000000;
	if (vmeIn->prefetchEnable)
		temp_ctl |= 0x20000000;
	if (vmeIn->rmwLock)
		temp_ctl |= 0x00000040;
	if (vmeIn->data64BitCapable)
		temp_ctl |= 0x00000080;
	if (vmeIn->userAccessType & VME_USER)
		temp_ctl |= 0x00100000;
	if (vmeIn->userAccessType & VME_SUPER)
		temp_ctl |= 0x00200000;
	if (vmeIn->dataAccessType & VME_DATA)
		temp_ctl |= 0x00400000;
	if (vmeIn->dataAccessType & VME_PROG)
		temp_ctl |= 0x00800000;

	// Write ctl reg without enable
	writel(temp_ctl, vmechip_baseaddr + inCTL[vmeIn->windowNbr]);

	if (vmeIn->windowEnable)
		temp_ctl |= 0x80000000;

	writel(temp_ctl, vmechip_baseaddr + inCTL[vmeIn->windowNbr]);
	return (0);
}

//-----------------------------------------------------------------------------
// Function   : uni_get_in_bound
// Description:
//-----------------------------------------------------------------------------
int uni_get_in_bound(vmeInWindowCfg_t * vmeIn)
{
	int temp_ctl = 0;

	// Verify input data
	if (vmeIn->windowNbr > 7) {
		return (-EINVAL);
	}
	// Get Window mappings.
	vmeIn->vmeAddrL = readl(vmechip_baseaddr + inBS[vmeIn->windowNbr]);
	vmeIn->pciAddrL = vmeIn->vmeAddrL +
	    readl(vmechip_baseaddr + inTO[vmeIn->windowNbr]);
	vmeIn->windowSizeL = readl(vmechip_baseaddr + inBD[vmeIn->windowNbr]) -
	    vmeIn->vmeAddrL;

	temp_ctl = readl(vmechip_baseaddr + inCTL[vmeIn->windowNbr]);

	// Get Control & BUS attributes
	if (temp_ctl & 0x40000000)
		vmeIn->wrPostEnable = 1;
	if (temp_ctl & 0x20000000)
		vmeIn->prefetchEnable = 1;
	if (temp_ctl & 0x00000040)
		vmeIn->rmwLock = 1;
	if (temp_ctl & 0x00000080)
		vmeIn->data64BitCapable = 1;
	if (temp_ctl & 0x00100000)
		vmeIn->userAccessType |= VME_USER;
	if (temp_ctl & 0x00200000)
		vmeIn->userAccessType |= VME_SUPER;
	if (temp_ctl & 0x00400000)
		vmeIn->dataAccessType |= VME_DATA;
	if (temp_ctl & 0x00800000)
		vmeIn->dataAccessType |= VME_PROG;
	if (temp_ctl & 0x80000000)
		vmeIn->windowEnable = 1;

	switch ((temp_ctl & 0x70000) >> 16) {
	case 0x0:
		vmeIn->addrSpace = VME_A16;
		break;
	case 0x1:
		vmeIn->addrSpace = VME_A24;
		break;
	case 0x2:
		vmeIn->addrSpace = VME_A32;
		break;
	case 0x6:
		vmeIn->addrSpace = VME_USER1;
		break;
	case 0x7:
		vmeIn->addrSpace = VME_USER2;
		break;
	}

	return (0);
}

//-----------------------------------------------------------------------------
// Function   : uni_set_out_bound
// Description:
//-----------------------------------------------------------------------------
int uni_set_out_bound(vmeOutWindowCfg_t * vmeOut)
{
	int temp_ctl = 0;

	// Verify input data
	if (vmeOut->windowNbr > 7) {
		return (-EINVAL);
	}
	if ((vmeOut->xlatedAddrU) || (vmeOut->windowSizeU)
	    || (vmeOut->pciBusAddrU)) {
		return (-EINVAL);
	}
	if ((vmeOut->xlatedAddrL & 0xFFF) ||
	    (vmeOut->windowSizeL & 0xFFF) || (vmeOut->pciBusAddrL & 0xFFF)) {
		return (-EINVAL);
	}
	if (vmeOut->bcastSelect2esst) {
		return (-EINVAL);
	}
	switch (vmeOut->addrSpace) {
	case VME_A64:
	case VME_USER3:
	case VME_USER4:
		return (-EINVAL);
	case VME_A16:
		temp_ctl |= 0x00000;
		break;
	case VME_A24:
		temp_ctl |= 0x10000;
		break;
	case VME_A32:
		temp_ctl |= 0x20000;
		break;
	case VME_CRCSR:
		temp_ctl |= 0x50000;
		break;
	case VME_USER1:
		temp_ctl |= 0x60000;
		break;
	case VME_USER2:
		temp_ctl |= 0x70000;
		break;
	}

	// Disable while we are mucking around
	writel(0x00000000, vmechip_baseaddr + outCTL[vmeOut->windowNbr]);
	writel(vmeOut->pciBusAddrL,
	       vmechip_baseaddr + outBS[vmeOut->windowNbr]);
	writel(vmeOut->pciBusAddrL + vmeOut->windowSizeL,
	       vmechip_baseaddr + outBD[vmeOut->windowNbr]);
	writel(vmeOut->xlatedAddrL - vmeOut->pciBusAddrL,
	       vmechip_baseaddr + outTO[vmeOut->windowNbr]);

	// Sanity check.
	if (vmeOut->pciBusAddrL !=
	    readl(vmechip_baseaddr + outBS[vmeOut->windowNbr])) {
		printk(KERN_ERR
		       "ca91c042: out window: %x, failed to configure\n",
		       vmeOut->windowNbr);
		return (-EINVAL);
	}

	if (vmeOut->pciBusAddrL + vmeOut->windowSizeL !=
	    readl(vmechip_baseaddr + outBD[vmeOut->windowNbr])) {
		printk(KERN_ERR
		       "ca91c042: out window: %x, failed to configure\n",
		       vmeOut->windowNbr);
		return (-EINVAL);
	}

	if (vmeOut->xlatedAddrL - vmeOut->pciBusAddrL !=
	    readl(vmechip_baseaddr + outTO[vmeOut->windowNbr])) {
		printk(KERN_ERR
		       "ca91c042: out window: %x, failed to configure\n",
		       vmeOut->windowNbr);
		return (-EINVAL);
	}
	// Setup CTL register.
	if (vmeOut->wrPostEnable)
		temp_ctl |= 0x40000000;
	if (vmeOut->userAccessType & VME_SUPER)
		temp_ctl |= 0x001000;
	if (vmeOut->dataAccessType & VME_PROG)
		temp_ctl |= 0x004000;
	if (vmeOut->maxDataWidth == VME_D16)
		temp_ctl |= 0x00400000;
	if (vmeOut->maxDataWidth == VME_D32)
		temp_ctl |= 0x00800000;
	if (vmeOut->maxDataWidth == VME_D64)
		temp_ctl |= 0x00C00000;
	if (vmeOut->xferProtocol & (VME_BLT | VME_MBLT))
		temp_ctl |= 0x00000100;

	// Write ctl reg without enable
	writel(temp_ctl, vmechip_baseaddr + outCTL[vmeOut->windowNbr]);

	if (vmeOut->windowEnable)
		temp_ctl |= 0x80000000;

	writel(temp_ctl, vmechip_baseaddr + outCTL[vmeOut->windowNbr]);
	return (0);
}

//-----------------------------------------------------------------------------
// Function   : uni_get_out_bound
// Description:
//-----------------------------------------------------------------------------
int uni_get_out_bound(vmeOutWindowCfg_t * vmeOut)
{
	int temp_ctl = 0;

	// Verify input data
	if (vmeOut->windowNbr > 7) {
		return (-EINVAL);
	}
	// Get Window mappings.
	vmeOut->pciBusAddrL =
	    readl(vmechip_baseaddr + outBS[vmeOut->windowNbr]);
	vmeOut->xlatedAddrL =
	    vmeOut->pciBusAddrL + readl(vmechip_baseaddr +
					outTO[vmeOut->windowNbr]);
	vmeOut->windowSizeL =
	    readl(vmechip_baseaddr + outBD[vmeOut->windowNbr]) -
	    vmeOut->pciBusAddrL;

	temp_ctl = readl(vmechip_baseaddr + outCTL[vmeOut->windowNbr]);

	// Get Control & BUS attributes
	if (temp_ctl & 0x40000000)
		vmeOut->wrPostEnable = 1;
	if (temp_ctl & 0x001000)
		vmeOut->userAccessType = VME_SUPER;
	else
		vmeOut->userAccessType = VME_USER;
	if (temp_ctl & 0x004000)
		vmeOut->dataAccessType = VME_PROG;
	else
		vmeOut->dataAccessType = VME_DATA;
	if (temp_ctl & 0x80000000)
		vmeOut->windowEnable = 1;

	switch ((temp_ctl & 0x00C00000) >> 22) {
	case 0:
		vmeOut->maxDataWidth = VME_D8;
		break;
	case 1:
		vmeOut->maxDataWidth = VME_D16;
		break;
	case 2:
		vmeOut->maxDataWidth = VME_D32;
		break;
	case 3:
		vmeOut->maxDataWidth = VME_D64;
		break;
	}
	if (temp_ctl & 0x00000100)
		vmeOut->xferProtocol = VME_BLT;
	else
		vmeOut->xferProtocol = VME_SCT;

	switch ((temp_ctl & 0x70000) >> 16) {
	case 0x0:
		vmeOut->addrSpace = VME_A16;
		break;
	case 0x1:
		vmeOut->addrSpace = VME_A24;
		break;
	case 0x2:
		vmeOut->addrSpace = VME_A32;
		break;
	case 0x5:
		vmeOut->addrSpace = VME_CRCSR;
		break;
	case 0x6:
		vmeOut->addrSpace = VME_USER1;
		break;
	case 0x7:
		vmeOut->addrSpace = VME_USER2;
		break;
	}

	return (0);
}

//-----------------------------------------------------------------------------
// Function   : uni_setup_lm
// Description:
//-----------------------------------------------------------------------------
int uni_setup_lm(vmeLmCfg_t * vmeLm)
{
	int temp_ctl = 0;

	if (vmeLm->addrU) {
		return (-EINVAL);
	}
	switch (vmeLm->addrSpace) {
	case VME_A64:
	case VME_USER3:
	case VME_USER4:
		return (-EINVAL);
	case VME_A16:
		temp_ctl |= 0x00000;
		break;
	case VME_A24:
		temp_ctl |= 0x10000;
		break;
	case VME_A32:
		temp_ctl |= 0x20000;
		break;
	case VME_CRCSR:
		temp_ctl |= 0x50000;
		break;
	case VME_USER1:
		temp_ctl |= 0x60000;
		break;
	case VME_USER2:
		temp_ctl |= 0x70000;
		break;
	}

	// Disable while we are mucking around
	writel(0x00000000, vmechip_baseaddr + LM_CTL);

	writel(vmeLm->addr, vmechip_baseaddr + LM_BS);

	// Setup CTL register.
	if (vmeLm->userAccessType & VME_SUPER)
		temp_ctl |= 0x00200000;
	if (vmeLm->userAccessType & VME_USER)
		temp_ctl |= 0x00100000;
	if (vmeLm->dataAccessType & VME_PROG)
		temp_ctl |= 0x00800000;
	if (vmeLm->dataAccessType & VME_DATA)
		temp_ctl |= 0x00400000;

	uni_lm_event = 0;

	// Write ctl reg and enable
	writel(0x80000000 | temp_ctl, vmechip_baseaddr + LM_CTL);
	temp_ctl = readl(vmechip_baseaddr + LM_CTL);

	return (0);
}

//-----------------------------------------------------------------------------
// Function   : uni_wait_lm
// Description:
//-----------------------------------------------------------------------------
int uni_wait_lm(vmeLmCfg_t * vmeLm)
{
	unsigned long flags;
	unsigned int tmp;

	spin_lock_irqsave(&lm_lock, flags);
	tmp = uni_lm_event;
	spin_unlock_irqrestore(&lm_lock, flags);
	if (tmp == 0) {
		if (vmeLm->lmWait < 10)
			vmeLm->lmWait = 10;
		interruptible_sleep_on_timeout(&lm_queue, vmeLm->lmWait);
	}
	writel(0x00000000, vmechip_baseaddr + LM_CTL);
	vmeLm->lmEvents = uni_lm_event;

	return (0);
}

#define	SWIZZLE(X) ( ((X & 0xFF000000) >> 24) | ((X & 0x00FF0000) >>  8) | ((X & 0x0000FF00) <<  8) | ((X & 0x000000FF) << 24))

//-----------------------------------------------------------------------------
// Function   : uni_do_rmw
// Description:
//-----------------------------------------------------------------------------
int uni_do_rmw(vmeRmwCfg_t * vmeRmw)
{
	int temp_ctl = 0;
	int tempBS = 0;
	int tempBD = 0;
	int tempTO = 0;
	int vmeBS = 0;
	int vmeBD = 0;
	int *rmw_pci_data_ptr = NULL;
	int *vaDataPtr = NULL;
	int i;
	vmeOutWindowCfg_t vmeOut;
	if (vmeRmw->maxAttempts < 1) {
		return (-EINVAL);
	}
	if (vmeRmw->targetAddrU) {
		return (-EINVAL);
	}
	// Find the PCI address that maps to the desired VME address
	for (i = 0; i < 8; i++) {
		temp_ctl = readl(vmechip_baseaddr + outCTL[i]);
		if ((temp_ctl & 0x80000000) == 0) {
			continue;
		}
		memset(&vmeOut, 0, sizeof(vmeOut));
		vmeOut.windowNbr = i;
		uni_get_out_bound(&vmeOut);
		if (vmeOut.addrSpace != vmeRmw->addrSpace) {
			continue;
		}
		tempBS = readl(vmechip_baseaddr + outBS[i]);
		tempBD = readl(vmechip_baseaddr + outBD[i]);
		tempTO = readl(vmechip_baseaddr + outTO[i]);
		vmeBS = tempBS + tempTO;
		vmeBD = tempBD + tempTO;
		if ((vmeRmw->targetAddr >= vmeBS) &&
		    (vmeRmw->targetAddr < vmeBD)) {
			rmw_pci_data_ptr =
			    (int *)(tempBS + (vmeRmw->targetAddr - vmeBS));
			vaDataPtr =
			    (int *)(out_image_va[i] +
				    (vmeRmw->targetAddr - vmeBS));
			break;
		}
	}

	// If no window - fail.
	if (rmw_pci_data_ptr == NULL) {
		return (-EINVAL);
	}
	// Setup the RMW registers.
	writel(0, vmechip_baseaddr + SCYC_CTL);
	writel(SWIZZLE(vmeRmw->enableMask), vmechip_baseaddr + SCYC_EN);
	writel(SWIZZLE(vmeRmw->compareData), vmechip_baseaddr + SCYC_CMP);
	writel(SWIZZLE(vmeRmw->swapData), vmechip_baseaddr + SCYC_SWP);
	writel((int)rmw_pci_data_ptr, vmechip_baseaddr + SCYC_ADDR);
	writel(1, vmechip_baseaddr + SCYC_CTL);

	// Run the RMW cycle until either success or max attempts.
	vmeRmw->numAttempts = 1;
	while (vmeRmw->numAttempts <= vmeRmw->maxAttempts) {

		if ((readl(vaDataPtr) & vmeRmw->enableMask) ==
		    (vmeRmw->swapData & vmeRmw->enableMask)) {

			writel(0, vmechip_baseaddr + SCYC_CTL);
			break;

		}
		vmeRmw->numAttempts++;
	}

	// If no success, set num Attempts to be greater than max attempts
	if (vmeRmw->numAttempts > vmeRmw->maxAttempts) {
		vmeRmw->numAttempts = vmeRmw->maxAttempts + 1;
	}

	return (0);
}

//-----------------------------------------------------------------------------
// Function   : uniSetupDctlReg
// Description:
//-----------------------------------------------------------------------------
int uniSetupDctlReg(vmeDmaPacket_t * vmeDma, int *dctlregreturn)
{
	unsigned int dctlreg = 0x80;
	struct vmeAttr *vmeAttr;

	if (vmeDma->srcBus == VME_DMA_VME) {
		dctlreg = 0;
		vmeAttr = &vmeDma->srcVmeAttr;
	} else {
		dctlreg = 0x80000000;
		vmeAttr = &vmeDma->dstVmeAttr;
	}

	switch (vmeAttr->maxDataWidth) {
	case VME_D8:
		break;
	case VME_D16:
		dctlreg |= 0x00400000;
		break;
	case VME_D32:
		dctlreg |= 0x00800000;
		break;
	case VME_D64:
		dctlreg |= 0x00C00000;
		break;
	}

	switch (vmeAttr->addrSpace) {
	case VME_A16:
		break;
	case VME_A24:
		dctlreg |= 0x00010000;
		break;
	case VME_A32:
		dctlreg |= 0x00020000;
		break;
	case VME_USER1:
		dctlreg |= 0x00060000;
		break;
	case VME_USER2:
		dctlreg |= 0x00070000;
		break;

	case VME_A64:		// not supported in Universe DMA
	case VME_CRCSR:
	case VME_USER3:
	case VME_USER4:
		return (-EINVAL);
		break;
	}
	if (vmeAttr->userAccessType == VME_PROG) {
		dctlreg |= 0x00004000;
	}
	if (vmeAttr->dataAccessType == VME_SUPER) {
		dctlreg |= 0x00001000;
	}
	if (vmeAttr->xferProtocol != VME_SCT) {
		dctlreg |= 0x00000100;
	}
	*dctlregreturn = dctlreg;
	return (0);
}

//-----------------------------------------------------------------------------
// Function   : uni_start_dma
// Description:
//-----------------------------------------------------------------------------
unsigned int
uni_start_dma(int channel, unsigned int dgcsreg, TDMA_Cmd_Packet * vmeLL)
{
	unsigned int val;

	// Setup registers as needed for direct or chained.
	if (dgcsreg & 0x8000000) {
		writel(0, vmechip_baseaddr + DTBC);
		writel((unsigned int)vmeLL, vmechip_baseaddr + DCPP);
	} else {
#if	0
		printk("Starting: DGCS = %08x\n", dgcsreg);
		printk("Starting: DVA  = %08x\n", readl(&vmeLL->dva));
		printk("Starting: DLV  = %08x\n", readl(&vmeLL->dlv));
		printk("Starting: DTBC = %08x\n", readl(&vmeLL->dtbc));
		printk("Starting: DCTL = %08x\n", readl(&vmeLL->dctl));
#endif
		// Write registers
		writel(readl(&vmeLL->dva), vmechip_baseaddr + DVA);
		writel(readl(&vmeLL->dlv), vmechip_baseaddr + DLA);
		writel(readl(&vmeLL->dtbc), vmechip_baseaddr + DTBC);
		writel(readl(&vmeLL->dctl), vmechip_baseaddr + DCTL);
		writel(0, vmechip_baseaddr + DCPP);
	}

	// Start the operation
	writel(dgcsreg, vmechip_baseaddr + DGCS);
	val = get_tbl();
	writel(dgcsreg | 0x8000000F, vmechip_baseaddr + DGCS);
	return (val);
}

//-----------------------------------------------------------------------------
// Function   : uni_setup_dma
// Description:
//-----------------------------------------------------------------------------
TDMA_Cmd_Packet *uni_setup_dma(vmeDmaPacket_t * vmeDma)
{
	vmeDmaPacket_t *vmeCur;
	int maxPerPage;
	int currentLLcount;
	TDMA_Cmd_Packet *startLL;
	TDMA_Cmd_Packet *currentLL;
	TDMA_Cmd_Packet *nextLL;
	unsigned int dctlreg = 0;

	maxPerPage = PAGESIZE / sizeof(TDMA_Cmd_Packet) - 1;
	startLL = (TDMA_Cmd_Packet *) __get_free_pages(GFP_KERNEL, 0);
	if (startLL == 0) {
		return (startLL);
	}
	// First allocate pages for descriptors and create linked list
	vmeCur = vmeDma;
	currentLL = startLL;
	currentLLcount = 0;
	while (vmeCur != 0) {
		if (vmeCur->pNextPacket != 0) {
			currentLL->dcpp = (unsigned int)(currentLL + 1);
			currentLLcount++;
			if (currentLLcount >= maxPerPage) {
				currentLL->dcpp =
				    __get_free_pages(GFP_KERNEL, 0);
				currentLLcount = 0;
			}
			currentLL = (TDMA_Cmd_Packet *) currentLL->dcpp;
		} else {
			currentLL->dcpp = (unsigned int)0;
		}
		vmeCur = vmeCur->pNextPacket;
	}

	// Next fill in information for each descriptor
	vmeCur = vmeDma;
	currentLL = startLL;
	while (vmeCur != 0) {
		if (vmeCur->srcBus == VME_DMA_VME) {
			writel(vmeCur->srcAddr, &currentLL->dva);
			writel(vmeCur->dstAddr, &currentLL->dlv);
		} else {
			writel(vmeCur->srcAddr, &currentLL->dlv);
			writel(vmeCur->dstAddr, &currentLL->dva);
		}
		uniSetupDctlReg(vmeCur, &dctlreg);
		writel(dctlreg, &currentLL->dctl);
		writel(vmeCur->byteCount, &currentLL->dtbc);

		currentLL = (TDMA_Cmd_Packet *) currentLL->dcpp;
		vmeCur = vmeCur->pNextPacket;
	}

	// Convert Links to PCI addresses.
	currentLL = startLL;
	while (currentLL != 0) {
		nextLL = (TDMA_Cmd_Packet *) currentLL->dcpp;
		if (nextLL == 0) {
			writel(1, &currentLL->dcpp);
		} else {
			writel((unsigned int)virt_to_bus(nextLL),
			       &currentLL->dcpp);
		}
		currentLL = nextLL;
	}

	// Return pointer to descriptors list
	return (startLL);
}

//-----------------------------------------------------------------------------
// Function   : uni_free_dma
// Description:
//-----------------------------------------------------------------------------
int uni_free_dma(TDMA_Cmd_Packet * startLL)
{
	TDMA_Cmd_Packet *currentLL;
	TDMA_Cmd_Packet *prevLL;
	TDMA_Cmd_Packet *nextLL;
	unsigned int dcppreg;

	// Convert Links to virtual addresses.
	currentLL = startLL;
	while (currentLL != 0) {
		dcppreg = readl(&currentLL->dcpp);
		dcppreg &= ~6;
		if (dcppreg & 1) {
			currentLL->dcpp = 0;
		} else {
			currentLL->dcpp = (unsigned int)bus_to_virt(dcppreg);
		}
		currentLL = (TDMA_Cmd_Packet *) currentLL->dcpp;
	}

	// Free all pages associated with the descriptors.
	currentLL = startLL;
	prevLL = currentLL;
	while (currentLL != 0) {
		nextLL = (TDMA_Cmd_Packet *) currentLL->dcpp;
		if (currentLL + 1 != nextLL) {
			free_pages((int)prevLL, 0);
			prevLL = nextLL;
		}
		currentLL = nextLL;
	}

	// Return pointer to descriptors list
	return (0);
}

//-----------------------------------------------------------------------------
// Function   : uni_do_dma
// Description:
//-----------------------------------------------------------------------------
int uni_do_dma(vmeDmaPacket_t * vmeDma)
{
	unsigned int dgcsreg = 0;
	unsigned int dctlreg = 0;
	int val;
	int channel, x;
	vmeDmaPacket_t *curDma;
	TDMA_Cmd_Packet *dmaLL;

	// Sanity check the VME chain.
	channel = vmeDma->channel_number;
	if (channel > 0) {
		return (-EINVAL);
	}
	curDma = vmeDma;
	while (curDma != 0) {
		if (curDma->byteCount == 0) {
			return (-EINVAL);
		}
		if (curDma->byteCount >= 0x1000000) {
			return (-EINVAL);
		}
		if ((curDma->srcAddr & 7) != (curDma->dstAddr & 7)) {
			return (-EINVAL);
		}
		switch (curDma->srcBus) {
		case VME_DMA_PCI:
			if (curDma->dstBus != VME_DMA_VME) {
				return (-EINVAL);
			}
			break;
		case VME_DMA_VME:
			if (curDma->dstBus != VME_DMA_PCI) {
				return (-EINVAL);
			}
			break;
		default:
			return (-EINVAL);
			break;
		}
		if (uniSetupDctlReg(curDma, &dctlreg) < 0) {
			return (-EINVAL);
		}

		curDma = curDma->pNextPacket;
		if (curDma == vmeDma) {	// Endless Loop!
			return (-EINVAL);
		}
	}

	// calculate control register
	if (vmeDma->pNextPacket != 0) {
		dgcsreg = 0x8000000;
	} else {
		dgcsreg = 0;
	}

	for (x = 0; x < 8; x++) {	// vme block size
		if ((256 << x) >= vmeDma->maxVmeBlockSize) {
			break;
		}
	}
	if (x == 8)
		x = 7;
	dgcsreg |= (x << 20);

	if (vmeDma->vmeBackOffTimer) {
		for (x = 1; x < 8; x++) {	// vme timer
			if ((16 << (x - 1)) >= vmeDma->vmeBackOffTimer) {
				break;
			}
		}
		if (x == 8)
			x = 7;
		dgcsreg |= (x << 16);
	}
	// Setup the dma chain
	dmaLL = uni_setup_dma(vmeDma);

	// Start the DMA
	if (dgcsreg & 0x8000000) {
		vmeDma->vmeDmaStartTick =
		    uni_start_dma(channel, dgcsreg,
				  (TDMA_Cmd_Packet *) virt_to_phys(dmaLL));
	} else {
		vmeDma->vmeDmaStartTick =
		    uni_start_dma(channel, dgcsreg, dmaLL);
	}

	wait_event_interruptible(dma_queue[0],
				 readl(vmechip_baseaddr + DGCS) & 0x800);

	val = readl(vmechip_baseaddr + DGCS);
	writel(val | 0xF00, vmechip_baseaddr + DGCS);

	vmeDma->vmeDmaStatus = 0;
	vmeDma->vmeDmaStopTick = uni_dma_irq_time;
	if (vmeDma->vmeDmaStopTick < vmeDma->vmeDmaStartTick) {
		vmeDma->vmeDmaElapsedTime =
		    (0xFFFFFFFF - vmeDma->vmeDmaStartTick) +
		    vmeDma->vmeDmaStopTick;
	} else {
		vmeDma->vmeDmaElapsedTime =
		    vmeDma->vmeDmaStopTick - vmeDma->vmeDmaStartTick;
	}
	vmeDma->vmeDmaElapsedTime -= vmechip_irq_overhead_ticks;
	vmeDma->vmeDmaElapsedTime /= (tb_speed / 1000000);

	if (!(val & 0x00000800)) {
		vmeDma->vmeDmaStatus = val & 0x700;
		printk(KERN_ERR
		       "ca91c042: DMA Error in DMA_uni_irqhandler DGCS=%08X\n",
		       val);
		val = readl(vmechip_baseaddr + DCPP);
		printk(KERN_ERR "ca91c042: DCPP=%08X\n", val);
		val = readl(vmechip_baseaddr + DCTL);
		printk(KERN_ERR "ca91c042: DCTL=%08X\n", val);
		val = readl(vmechip_baseaddr + DTBC);
		printk(KERN_ERR "ca91c042: DTBC=%08X\n", val);
		val = readl(vmechip_baseaddr + DLA);
		printk(KERN_ERR "ca91c042: DLA=%08X\n", val);
		val = readl(vmechip_baseaddr + DVA);
		printk(KERN_ERR "ca91c042: DVA=%08X\n", val);

	}
	// Free the dma chain
	uni_free_dma(dmaLL);

	return (0);
}

//-----------------------------------------------------------------------------
// Function   : uni_shutdown
// Description: Put VME bridge in quiescent state.
//-----------------------------------------------------------------------------
void uni_shutdown(void)
{
	writel(0, vmechip_baseaddr + LINT_EN);	// Turn off Ints

	// Turn off the windows
	writel(0x00800000, vmechip_baseaddr + LSI0_CTL);
	writel(0x00800000, vmechip_baseaddr + LSI1_CTL);
	writel(0x00800000, vmechip_baseaddr + LSI2_CTL);
	writel(0x00800000, vmechip_baseaddr + LSI3_CTL);
	writel(0x00F00000, vmechip_baseaddr + VSI0_CTL);
	writel(0x00F00000, vmechip_baseaddr + VSI1_CTL);
	writel(0x00F00000, vmechip_baseaddr + VSI2_CTL);
	writel(0x00F00000, vmechip_baseaddr + VSI3_CTL);
	if (vmechip_revision >= 2) {
		writel(0x00800000, vmechip_baseaddr + LSI4_CTL);
		writel(0x00800000, vmechip_baseaddr + LSI5_CTL);
		writel(0x00800000, vmechip_baseaddr + LSI6_CTL);
		writel(0x00800000, vmechip_baseaddr + LSI7_CTL);
		writel(0x00F00000, vmechip_baseaddr + VSI4_CTL);
		writel(0x00F00000, vmechip_baseaddr + VSI5_CTL);
		writel(0x00F00000, vmechip_baseaddr + VSI6_CTL);
		writel(0x00F00000, vmechip_baseaddr + VSI7_CTL);
	}
}

//-----------------------------------------------------------------------------
// Function   : uni_init()
// Description:
//-----------------------------------------------------------------------------
int uni_init(void)
{
	int result;
	unsigned int tmp;
	unsigned int crcsr_addr;
	unsigned int irqOverHeadStart;
	int overHeadTicks;

	uni_shutdown();

	// Write to Misc Register
	// Set VME Bus Time-out
	//   Arbitration Mode
	//   DTACK Enable
	tmp = readl(vmechip_baseaddr + MISC_CTL) & 0x0832BFFF;
	tmp |= 0x76040000;
	writel(tmp, vmechip_baseaddr + MISC_CTL);
	if (tmp & 0x20000) {
		vme_syscon = 1;
	} else {
		vme_syscon = 0;
	}

	// Clear DMA status log
	writel(0x00000F00, vmechip_baseaddr + DGCS);
	// Clear and enable error log
	writel(0x00800000, vmechip_baseaddr + L_CMDERR);
	// Turn off location monitor
	writel(0x00000000, vmechip_baseaddr + LM_CTL);

	// Initialize crcsr map
	if (vme_slotnum != -1) {
		writel(vme_slotnum << 27, vmechip_baseaddr + VCSR_BS);
	}
	crcsr_addr = readl(vmechip_baseaddr + VCSR_BS) >> 8;
	writel((unsigned int)vmechip_interboard_datap - crcsr_addr,
	       vmechip_baseaddr + VCSR_TO);
	if (vme_slotnum != -1) {
		writel(0x80000000, vmechip_baseaddr + VCSR_CTL);
	}
	// Turn off interrupts
	writel(0x00000000, vmechip_baseaddr + LINT_EN);	// Disable interrupts in the Universe first
	writel(0x00FFFFFF, vmechip_baseaddr + LINT_STAT);	// Clear Any Pending Interrupts
	writel(0x00000000, vmechip_baseaddr + VINT_EN);	// Disable interrupts in the Universe first

	result =
	    request_irq(vmechip_irq, uni_irqhandler, IRQF_SHARED | IRQF_DISABLED,
			"VMEBus (ca91c042)", vmechip_baseaddr);
	if (result) {
		printk(KERN_ERR
		       "ca91c042: can't get assigned pci irq vector %02X\n",
		       vmechip_irq);
		return (0);
	} else {
		writel(0x0000, vmechip_baseaddr + LINT_MAP0);	// Map all ints to 0
		writel(0x0000, vmechip_baseaddr + LINT_MAP1);	// Map all ints to 0
		writel(0x0000, vmechip_baseaddr + LINT_MAP2);	// Map all ints to 0
	}

	// Enable DMA, mailbox, VIRQ & LM Interrupts
	if (vme_syscon)
		tmp = 0x00FF07FE;
	else
		tmp = 0x00FF0700;
	writel(tmp, vmechip_baseaddr + LINT_EN);

	// Do a quick sanity test of the bridge
	if (readl(vmechip_baseaddr + LINT_EN) != tmp) {
		return (0);
	}
	if (readl(vmechip_baseaddr + PCI_CLASS_REVISION) != 0x06800002) {
		return (0);
	}
	for (tmp = 1; tmp < 0x80000000; tmp = tmp << 1) {
		writel(tmp, vmechip_baseaddr + SCYC_EN);
		writel(~tmp, vmechip_baseaddr + SCYC_CMP);
		if (readl(vmechip_baseaddr + SCYC_EN) != tmp) {
			return (0);
		}
		if (readl(vmechip_baseaddr + SCYC_CMP) != ~tmp) {
			return (0);
		}
	}

	// do a mail box interrupt to calibrate the interrupt overhead.

	irqOverHeadStart = get_tbl();
	writel(0, vmechip_baseaddr + MBOX1);
	for (tmp = 0; tmp < 10; tmp++) {
	}

	irqOverHeadStart = get_tbl();
	writel(0, vmechip_baseaddr + MBOX1);
	for (tmp = 0; tmp < 10; tmp++) {
	}

	overHeadTicks = uni_irq_time - irqOverHeadStart;
	if (overHeadTicks > 0) {
		vmechip_irq_overhead_ticks = overHeadTicks;
	} else {
		vmechip_irq_overhead_ticks = 1;
	}
	return (1);
}
