/*
 * Copyright (C) 2006 Freescale Semicondutor, Inc. All rights reserved.
 *
 * Authors: 	Shlomi Gridish <gridish@freescale.com>
 * 		Li Yang <leoli@freescale.com>
 * Based on cpm2_common.c from Dan Malek (dmalek@jlc.net)
 *
 * Description:
 * General Purpose functions for the global management of the
 * QUICC Engine (QE).
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/bootmem.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <asm/irq.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/immap_qe.h>
#include <asm/qe.h>
#include <asm/prom.h>
#include <asm/rheap.h>

static void qe_snums_init(void);
static void qe_muram_init(void);
static int qe_sdma_init(void);

static DEFINE_SPINLOCK(qe_lock);

/* QE snum state */
enum qe_snum_state {
	QE_SNUM_STATE_USED,
	QE_SNUM_STATE_FREE
};

/* QE snum */
struct qe_snum {
	u8 num;
	enum qe_snum_state state;
};

/* We allocate this here because it is used almost exclusively for
 * the communication processor devices.
 */
struct qe_immap *qe_immr = NULL;
EXPORT_SYMBOL(qe_immr);

static struct qe_snum snums[QE_NUM_OF_SNUM];	/* Dynamically allocated SNUMs */

static phys_addr_t qebase = -1;

phys_addr_t get_qe_base(void)
{
	struct device_node *qe;

	if (qebase != -1)
		return qebase;

	qe = of_find_node_by_type(NULL, "qe");
	if (qe) {
		unsigned int size;
		const void *prop = of_get_property(qe, "reg", &size);
		qebase = of_translate_address(qe, prop);
		of_node_put(qe);
	};

	return qebase;
}

EXPORT_SYMBOL(get_qe_base);

void qe_reset(void)
{
	if (qe_immr == NULL)
		qe_immr = ioremap(get_qe_base(), QE_IMMAP_SIZE);

	qe_snums_init();

	qe_issue_cmd(QE_RESET, QE_CR_SUBBLOCK_INVALID,
		     QE_CR_PROTOCOL_UNSPECIFIED, 0);

	/* Reclaim the MURAM memory for our use. */
	qe_muram_init();

	if (qe_sdma_init())
		panic("sdma init failed!");
}

int qe_issue_cmd(u32 cmd, u32 device, u8 mcn_protocol, u32 cmd_input)
{
	unsigned long flags;
	u8 mcn_shift = 0, dev_shift = 0;

	spin_lock_irqsave(&qe_lock, flags);
	if (cmd == QE_RESET) {
		out_be32(&qe_immr->cp.cecr, (u32) (cmd | QE_CR_FLG));
	} else {
		if (cmd == QE_ASSIGN_PAGE) {
			/* Here device is the SNUM, not sub-block */
			dev_shift = QE_CR_SNUM_SHIFT;
		} else if (cmd == QE_ASSIGN_RISC) {
			/* Here device is the SNUM, and mcnProtocol is
			 * e_QeCmdRiscAssignment value */
			dev_shift = QE_CR_SNUM_SHIFT;
			mcn_shift = QE_CR_MCN_RISC_ASSIGN_SHIFT;
		} else {
			if (device == QE_CR_SUBBLOCK_USB)
				mcn_shift = QE_CR_MCN_USB_SHIFT;
			else
				mcn_shift = QE_CR_MCN_NORMAL_SHIFT;
		}

		out_be32(&qe_immr->cp.cecdr, cmd_input);
		out_be32(&qe_immr->cp.cecr,
			 (cmd | QE_CR_FLG | ((u32) device << dev_shift) | (u32)
			  mcn_protocol << mcn_shift));
	}

	/* wait for the QE_CR_FLG to clear */
	while(in_be32(&qe_immr->cp.cecr) & QE_CR_FLG)
		cpu_relax();
	spin_unlock_irqrestore(&qe_lock, flags);

	return 0;
}
EXPORT_SYMBOL(qe_issue_cmd);

/* Set a baud rate generator. This needs lots of work. There are
 * 16 BRGs, which can be connected to the QE channels or output
 * as clocks. The BRGs are in two different block of internal
 * memory mapped space.
 * The BRG clock is the QE clock divided by 2.
 * It was set up long ago during the initial boot phase and is
 * is given to us.
 * Baud rate clocks are zero-based in the driver code (as that maps
 * to port numbers). Documentation uses 1-based numbering.
 */
static unsigned int brg_clk = 0;

unsigned int get_brg_clk(void)
{
	struct device_node *qe;
	if (brg_clk)
		return brg_clk;

	qe = of_find_node_by_type(NULL, "qe");
	if (qe) {
		unsigned int size;
		const u32 *prop = of_get_property(qe, "brg-frequency", &size);
		brg_clk = *prop;
		of_node_put(qe);
	};
	return brg_clk;
}

/* Program the BRG to the given sampling rate and multiplier
 *
 * @brg: the BRG, 1-16
 * @rate: the desired sampling rate
 * @multiplier: corresponds to the value programmed in GUMR_L[RDCR] or
 * GUMR_L[TDCR].  E.g., if this BRG is the RX clock, and GUMR_L[RDCR]=01,
 * then 'multiplier' should be 8.
 *
 * Also note that the value programmed into the BRGC register must be even.
 */
void qe_setbrg(unsigned int brg, unsigned int rate, unsigned int multiplier)
{
	u32 divisor, tempval;
	u32 div16 = 0;

	divisor = get_brg_clk() / (rate * multiplier);

	if (divisor > QE_BRGC_DIVISOR_MAX + 1) {
		div16 = QE_BRGC_DIV16;
		divisor /= 16;
	}

	/* Errata QE_General4, which affects some MPC832x and MPC836x SOCs, says
	   that the BRG divisor must be even if you're not using divide-by-16
	   mode. */
	if (!div16 && (divisor & 1))
		divisor++;

	tempval = ((divisor - 1) << QE_BRGC_DIVISOR_SHIFT) |
		QE_BRGC_ENABLE | div16;

	out_be32(&qe_immr->brg.brgc[brg - 1], tempval);
}

/* Initialize SNUMs (thread serial numbers) according to
 * QE Module Control chapter, SNUM table
 */
static void qe_snums_init(void)
{
	int i;
	static const u8 snum_init[] = {
		0x04, 0x05, 0x0C, 0x0D, 0x14, 0x15, 0x1C, 0x1D,
		0x24, 0x25, 0x2C, 0x2D, 0x34, 0x35, 0x88, 0x89,
		0x98, 0x99, 0xA8, 0xA9, 0xB8, 0xB9, 0xC8, 0xC9,
		0xD8, 0xD9, 0xE8, 0xE9,
	};

	for (i = 0; i < QE_NUM_OF_SNUM; i++) {
		snums[i].num = snum_init[i];
		snums[i].state = QE_SNUM_STATE_FREE;
	}
}

int qe_get_snum(void)
{
	unsigned long flags;
	int snum = -EBUSY;
	int i;

	spin_lock_irqsave(&qe_lock, flags);
	for (i = 0; i < QE_NUM_OF_SNUM; i++) {
		if (snums[i].state == QE_SNUM_STATE_FREE) {
			snums[i].state = QE_SNUM_STATE_USED;
			snum = snums[i].num;
			break;
		}
	}
	spin_unlock_irqrestore(&qe_lock, flags);

	return snum;
}
EXPORT_SYMBOL(qe_get_snum);

void qe_put_snum(u8 snum)
{
	int i;

	for (i = 0; i < QE_NUM_OF_SNUM; i++) {
		if (snums[i].num == snum) {
			snums[i].state = QE_SNUM_STATE_FREE;
			break;
		}
	}
}
EXPORT_SYMBOL(qe_put_snum);

static int qe_sdma_init(void)
{
	struct sdma *sdma = &qe_immr->sdma;
	unsigned long sdma_buf_offset;

	if (!sdma)
		return -ENODEV;

	/* allocate 2 internal temporary buffers (512 bytes size each) for
	 * the SDMA */
 	sdma_buf_offset = qe_muram_alloc(512 * 2, 4096);
	if (IS_ERR_VALUE(sdma_buf_offset))
		return -ENOMEM;

	out_be32(&sdma->sdebcr, (u32) sdma_buf_offset & QE_SDEBCR_BA_MASK);
 	out_be32(&sdma->sdmr, (QE_SDMR_GLB_1_MSK |
 					(0x1 << QE_SDMR_CEN_SHIFT)));

	return 0;
}

/*
 * muram_alloc / muram_free bits.
 */
static DEFINE_SPINLOCK(qe_muram_lock);

/* 16 blocks should be enough to satisfy all requests
 * until the memory subsystem goes up... */
static rh_block_t qe_boot_muram_rh_block[16];
static rh_info_t qe_muram_info;

static void qe_muram_init(void)
{
	struct device_node *np;
	u32 address;
	u64 size;
	unsigned int flags;

	/* initialize the info header */
	rh_init(&qe_muram_info, 1,
		sizeof(qe_boot_muram_rh_block) /
		sizeof(qe_boot_muram_rh_block[0]), qe_boot_muram_rh_block);

	/* Attach the usable muram area */
	/* XXX: This is a subset of the available muram. It
	 * varies with the processor and the microcode patches activated.
	 */
	if ((np = of_find_node_by_name(NULL, "data-only")) != NULL) {
		address = *of_get_address(np, 0, &size, &flags);
		of_node_put(np);
		rh_attach_region(&qe_muram_info, address, (int) size);
	}
}

/* This function returns an index into the MURAM area.
 */
unsigned long qe_muram_alloc(int size, int align)
{
	unsigned long start;
	unsigned long flags;

	spin_lock_irqsave(&qe_muram_lock, flags);
	start = rh_alloc_align(&qe_muram_info, size, align, "QE");
	spin_unlock_irqrestore(&qe_muram_lock, flags);

	return start;
}
EXPORT_SYMBOL(qe_muram_alloc);

int qe_muram_free(unsigned long offset)
{
	int ret;
	unsigned long flags;

	spin_lock_irqsave(&qe_muram_lock, flags);
	ret = rh_free(&qe_muram_info, offset);
	spin_unlock_irqrestore(&qe_muram_lock, flags);

	return ret;
}
EXPORT_SYMBOL(qe_muram_free);

/* not sure if this is ever needed */
unsigned long qe_muram_alloc_fixed(unsigned long offset, int size)
{
	unsigned long start;
	unsigned long flags;

	spin_lock_irqsave(&qe_muram_lock, flags);
	start = rh_alloc_fixed(&qe_muram_info, offset, size, "commproc");
	spin_unlock_irqrestore(&qe_muram_lock, flags);

	return start;
}
EXPORT_SYMBOL(qe_muram_alloc_fixed);

void qe_muram_dump(void)
{
	rh_dump(&qe_muram_info);
}
EXPORT_SYMBOL(qe_muram_dump);

void *qe_muram_addr(unsigned long offset)
{
	return (void *)&qe_immr->muram[offset];
}
EXPORT_SYMBOL(qe_muram_addr);
