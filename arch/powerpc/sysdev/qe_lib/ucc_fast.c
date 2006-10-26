/*
 * arch/powerpc/sysdev/qe_lib/ucc_fast.c
 *
 * QE UCC Fast API Set - UCC Fast specific routines implementations.
 *
 * Copyright (C) 2006 Freescale Semicondutor, Inc. All rights reserved.
 *
 * Authors: 	Shlomi Gridish <gridish@freescale.com>
 * 		Li Yang <leoli@freescale.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/stddef.h>
#include <linux/interrupt.h>

#include <asm/io.h>
#include <asm/immap_qe.h>
#include <asm/qe.h>

#include <asm/ucc.h>
#include <asm/ucc_fast.h>

#define uccf_printk(level, format, arg...) \
	printk(level format "\n", ## arg)

#define uccf_dbg(format, arg...) \
	uccf_printk(KERN_DEBUG , format , ## arg)
#define uccf_err(format, arg...) \
	uccf_printk(KERN_ERR , format , ## arg)
#define uccf_info(format, arg...) \
	uccf_printk(KERN_INFO , format , ## arg)
#define uccf_warn(format, arg...) \
	uccf_printk(KERN_WARNING , format , ## arg)

#ifdef UCCF_VERBOSE_DEBUG
#define uccf_vdbg uccf_dbg
#else
#define uccf_vdbg(fmt, args...) do { } while (0)
#endif				/* UCCF_VERBOSE_DEBUG */

void ucc_fast_dump_regs(struct ucc_fast_private * uccf)
{
	uccf_info("UCC%d Fast registers:", uccf->uf_info->ucc_num);
	uccf_info("Base address: 0x%08x", (u32) uccf->uf_regs);

	uccf_info("gumr  : addr - 0x%08x, val - 0x%08x",
		  (u32) & uccf->uf_regs->gumr, in_be32(&uccf->uf_regs->gumr));
	uccf_info("upsmr : addr - 0x%08x, val - 0x%08x",
		  (u32) & uccf->uf_regs->upsmr, in_be32(&uccf->uf_regs->upsmr));
	uccf_info("utodr : addr - 0x%08x, val - 0x%04x",
		  (u32) & uccf->uf_regs->utodr, in_be16(&uccf->uf_regs->utodr));
	uccf_info("udsr  : addr - 0x%08x, val - 0x%04x",
		  (u32) & uccf->uf_regs->udsr, in_be16(&uccf->uf_regs->udsr));
	uccf_info("ucce  : addr - 0x%08x, val - 0x%08x",
		  (u32) & uccf->uf_regs->ucce, in_be32(&uccf->uf_regs->ucce));
	uccf_info("uccm  : addr - 0x%08x, val - 0x%08x",
		  (u32) & uccf->uf_regs->uccm, in_be32(&uccf->uf_regs->uccm));
	uccf_info("uccs  : addr - 0x%08x, val - 0x%02x",
		  (u32) & uccf->uf_regs->uccs, uccf->uf_regs->uccs);
	uccf_info("urfb  : addr - 0x%08x, val - 0x%08x",
		  (u32) & uccf->uf_regs->urfb, in_be32(&uccf->uf_regs->urfb));
	uccf_info("urfs  : addr - 0x%08x, val - 0x%04x",
		  (u32) & uccf->uf_regs->urfs, in_be16(&uccf->uf_regs->urfs));
	uccf_info("urfet : addr - 0x%08x, val - 0x%04x",
		  (u32) & uccf->uf_regs->urfet, in_be16(&uccf->uf_regs->urfet));
	uccf_info("urfset: addr - 0x%08x, val - 0x%04x",
		  (u32) & uccf->uf_regs->urfset,
		  in_be16(&uccf->uf_regs->urfset));
	uccf_info("utfb  : addr - 0x%08x, val - 0x%08x",
		  (u32) & uccf->uf_regs->utfb, in_be32(&uccf->uf_regs->utfb));
	uccf_info("utfs  : addr - 0x%08x, val - 0x%04x",
		  (u32) & uccf->uf_regs->utfs, in_be16(&uccf->uf_regs->utfs));
	uccf_info("utfet : addr - 0x%08x, val - 0x%04x",
		  (u32) & uccf->uf_regs->utfet, in_be16(&uccf->uf_regs->utfet));
	uccf_info("utftt : addr - 0x%08x, val - 0x%04x",
		  (u32) & uccf->uf_regs->utftt, in_be16(&uccf->uf_regs->utftt));
	uccf_info("utpt  : addr - 0x%08x, val - 0x%04x",
		  (u32) & uccf->uf_regs->utpt, in_be16(&uccf->uf_regs->utpt));
	uccf_info("urtry : addr - 0x%08x, val - 0x%08x",
		  (u32) & uccf->uf_regs->urtry, in_be32(&uccf->uf_regs->urtry));
	uccf_info("guemr : addr - 0x%08x, val - 0x%02x",
		  (u32) & uccf->uf_regs->guemr, uccf->uf_regs->guemr);
}

u32 ucc_fast_get_qe_cr_subblock(int uccf_num)
{
	switch (uccf_num) {
	case 0:	return QE_CR_SUBBLOCK_UCCFAST1;
	case 1: return QE_CR_SUBBLOCK_UCCFAST2;
	case 2: return QE_CR_SUBBLOCK_UCCFAST3;
	case 3: return QE_CR_SUBBLOCK_UCCFAST4;
	case 4: return QE_CR_SUBBLOCK_UCCFAST5;
	case 5: return QE_CR_SUBBLOCK_UCCFAST6;
	case 6: return QE_CR_SUBBLOCK_UCCFAST7;
	case 7:	return QE_CR_SUBBLOCK_UCCFAST8;
	default: return QE_CR_SUBBLOCK_INVALID;
	}
}

void ucc_fast_transmit_on_demand(struct ucc_fast_private * uccf)
{
	out_be16(&uccf->uf_regs->utodr, UCC_FAST_TOD);
}

void ucc_fast_enable(struct ucc_fast_private * uccf, enum comm_dir mode)
{
	struct ucc_fast *uf_regs;
	u32 gumr;

	uf_regs = uccf->uf_regs;

	/* Enable reception and/or transmission on this UCC. */
	gumr = in_be32(&uf_regs->gumr);
	if (mode & COMM_DIR_TX) {
		gumr |= UCC_FAST_GUMR_ENT;
		uccf->enabled_tx = 1;
	}
	if (mode & COMM_DIR_RX) {
		gumr |= UCC_FAST_GUMR_ENR;
		uccf->enabled_rx = 1;
	}
	out_be32(&uf_regs->gumr, gumr);
}

void ucc_fast_disable(struct ucc_fast_private * uccf, enum comm_dir mode)
{
	struct ucc_fast *uf_regs;
	u32 gumr;

	uf_regs = uccf->uf_regs;

	/* Disable reception and/or transmission on this UCC. */
	gumr = in_be32(&uf_regs->gumr);
	if (mode & COMM_DIR_TX) {
		gumr &= ~UCC_FAST_GUMR_ENT;
		uccf->enabled_tx = 0;
	}
	if (mode & COMM_DIR_RX) {
		gumr &= ~UCC_FAST_GUMR_ENR;
		uccf->enabled_rx = 0;
	}
	out_be32(&uf_regs->gumr, gumr);
}

int ucc_fast_init(struct ucc_fast_info * uf_info, struct ucc_fast_private ** uccf_ret)
{
	struct ucc_fast_private *uccf;
	struct ucc_fast *uf_regs;
	u32 gumr = 0;
	int ret;

	uccf_vdbg("%s: IN", __FUNCTION__);

	if (!uf_info)
		return -EINVAL;

	/* check if the UCC port number is in range. */
	if ((uf_info->ucc_num < 0) || (uf_info->ucc_num > UCC_MAX_NUM - 1)) {
		uccf_err("ucc_fast_init: Illegal UCC number!");
		return -EINVAL;
	}

	/* Check that 'max_rx_buf_length' is properly aligned (4). */
	if (uf_info->max_rx_buf_length & (UCC_FAST_MRBLR_ALIGNMENT - 1)) {
		uccf_err("ucc_fast_init: max_rx_buf_length not aligned.");
		return -EINVAL;
	}

	/* Validate Virtual Fifo register values */
	if (uf_info->urfs < UCC_FAST_URFS_MIN_VAL) {
		uccf_err
		    ("ucc_fast_init: Virtual Fifo register urfs too small.");
		return -EINVAL;
	}

	if (uf_info->urfs & (UCC_FAST_VIRT_FIFO_REGS_ALIGNMENT - 1)) {
		uccf_err
		    ("ucc_fast_init: Virtual Fifo register urfs not aligned.");
		return -EINVAL;
	}

	if (uf_info->urfet & (UCC_FAST_VIRT_FIFO_REGS_ALIGNMENT - 1)) {
		uccf_err
		    ("ucc_fast_init: Virtual Fifo register urfet not aligned.");
		return -EINVAL;
	}

	if (uf_info->urfset & (UCC_FAST_VIRT_FIFO_REGS_ALIGNMENT - 1)) {
		uccf_err
		   ("ucc_fast_init: Virtual Fifo register urfset not aligned.");
		return -EINVAL;
	}

	if (uf_info->utfs & (UCC_FAST_VIRT_FIFO_REGS_ALIGNMENT - 1)) {
		uccf_err
		    ("ucc_fast_init: Virtual Fifo register utfs not aligned.");
		return -EINVAL;
	}

	if (uf_info->utfet & (UCC_FAST_VIRT_FIFO_REGS_ALIGNMENT - 1)) {
		uccf_err
		    ("ucc_fast_init: Virtual Fifo register utfet not aligned.");
		return -EINVAL;
	}

	if (uf_info->utftt & (UCC_FAST_VIRT_FIFO_REGS_ALIGNMENT - 1)) {
		uccf_err
		    ("ucc_fast_init: Virtual Fifo register utftt not aligned.");
		return -EINVAL;
	}

	uccf = (struct ucc_fast_private *)
		 kmalloc(sizeof(struct ucc_fast_private), GFP_KERNEL);
	if (!uccf) {
		uccf_err
		    ("ucc_fast_init: No memory for UCC slow data structure!");
		return -ENOMEM;
	}
	memset(uccf, 0, sizeof(struct ucc_fast_private));

	/* Fill fast UCC structure */
	uccf->uf_info = uf_info;
	/* Set the PHY base address */
	uccf->uf_regs =
	    (struct ucc_fast *) ioremap(uf_info->regs, sizeof(struct ucc_fast));
	if (uccf->uf_regs == NULL) {
		uccf_err
		    ("ucc_fast_init: No memory map for UCC slow controller!");
		return -ENOMEM;
	}

	uccf->enabled_tx = 0;
	uccf->enabled_rx = 0;
	uccf->stopped_tx = 0;
	uccf->stopped_rx = 0;
	uf_regs = uccf->uf_regs;
	uccf->p_ucce = (u32 *) & (uf_regs->ucce);
	uccf->p_uccm = (u32 *) & (uf_regs->uccm);
#ifdef STATISTICS
	uccf->tx_frames = 0;
	uccf->rx_frames = 0;
	uccf->rx_discarded = 0;
#endif				/* STATISTICS */

	/* Init Guemr register */
	if ((ret = ucc_init_guemr((struct ucc_common *) (uf_regs)))) {
		uccf_err("ucc_fast_init: Could not init the guemr register.");
		ucc_fast_free(uccf);
		return ret;
	}

	/* Set UCC to fast type */
	if ((ret = ucc_set_type(uf_info->ucc_num,
				(struct ucc_common *) (uf_regs),
				UCC_SPEED_TYPE_FAST))) {
		uccf_err("ucc_fast_init: Could not set type to fast.");
		ucc_fast_free(uccf);
		return ret;
	}

	uccf->mrblr = uf_info->max_rx_buf_length;

	/* Set GUMR */
	/* For more details see the hardware spec. */
	/* gumr starts as zero. */
	if (uf_info->tci)
		gumr |= UCC_FAST_GUMR_TCI;
	gumr |= uf_info->ttx_trx;
	if (uf_info->cdp)
		gumr |= UCC_FAST_GUMR_CDP;
	if (uf_info->ctsp)
		gumr |= UCC_FAST_GUMR_CTSP;
	if (uf_info->cds)
		gumr |= UCC_FAST_GUMR_CDS;
	if (uf_info->ctss)
		gumr |= UCC_FAST_GUMR_CTSS;
	if (uf_info->txsy)
		gumr |= UCC_FAST_GUMR_TXSY;
	if (uf_info->rsyn)
		gumr |= UCC_FAST_GUMR_RSYN;
	gumr |= uf_info->synl;
	if (uf_info->rtsm)
		gumr |= UCC_FAST_GUMR_RTSM;
	gumr |= uf_info->renc;
	if (uf_info->revd)
		gumr |= UCC_FAST_GUMR_REVD;
	gumr |= uf_info->tenc;
	gumr |= uf_info->tcrc;
	gumr |= uf_info->mode;
	out_be32(&uf_regs->gumr, gumr);

	/* Allocate memory for Tx Virtual Fifo */
	uccf->ucc_fast_tx_virtual_fifo_base_offset =
	    qe_muram_alloc(uf_info->utfs, UCC_FAST_VIRT_FIFO_REGS_ALIGNMENT);
	if (IS_MURAM_ERR(uccf->ucc_fast_tx_virtual_fifo_base_offset)) {
		uccf_err
		    ("ucc_fast_init: Can not allocate MURAM memory for "
			"struct ucc_fastx_virtual_fifo_base_offset.");
		uccf->ucc_fast_tx_virtual_fifo_base_offset = 0;
		ucc_fast_free(uccf);
		return -ENOMEM;
	}

	/* Allocate memory for Rx Virtual Fifo */
	uccf->ucc_fast_rx_virtual_fifo_base_offset =
	    qe_muram_alloc(uf_info->urfs +
			   (u32)
			   UCC_FAST_RECEIVE_VIRTUAL_FIFO_SIZE_FUDGE_FACTOR,
			   UCC_FAST_VIRT_FIFO_REGS_ALIGNMENT);
	if (IS_MURAM_ERR(uccf->ucc_fast_rx_virtual_fifo_base_offset)) {
		uccf_err
		    ("ucc_fast_init: Can not allocate MURAM memory for "
			"ucc_fast_rx_virtual_fifo_base_offset.");
		uccf->ucc_fast_rx_virtual_fifo_base_offset = 0;
		ucc_fast_free(uccf);
		return -ENOMEM;
	}

	/* Set Virtual Fifo registers */
	out_be16(&uf_regs->urfs, uf_info->urfs);
	out_be16(&uf_regs->urfet, uf_info->urfet);
	out_be16(&uf_regs->urfset, uf_info->urfset);
	out_be16(&uf_regs->utfs, uf_info->utfs);
	out_be16(&uf_regs->utfet, uf_info->utfet);
	out_be16(&uf_regs->utftt, uf_info->utftt);
	/* utfb, urfb are offsets from MURAM base */
	out_be32(&uf_regs->utfb, uccf->ucc_fast_tx_virtual_fifo_base_offset);
	out_be32(&uf_regs->urfb, uccf->ucc_fast_rx_virtual_fifo_base_offset);

	/* Mux clocking */
	/* Grant Support */
	ucc_set_qe_mux_grant(uf_info->ucc_num, uf_info->grant_support);
	/* Breakpoint Support */
	ucc_set_qe_mux_bkpt(uf_info->ucc_num, uf_info->brkpt_support);
	/* Set Tsa or NMSI mode. */
	ucc_set_qe_mux_tsa(uf_info->ucc_num, uf_info->tsa);
	/* If NMSI (not Tsa), set Tx and Rx clock. */
	if (!uf_info->tsa) {
		/* Rx clock routing */
		if (uf_info->rx_clock != QE_CLK_NONE) {
			if (ucc_set_qe_mux_rxtx
			    (uf_info->ucc_num, uf_info->rx_clock,
			     COMM_DIR_RX)) {
				uccf_err
		("ucc_fast_init: Illegal value for parameter 'RxClock'.");
				ucc_fast_free(uccf);
				return -EINVAL;
			}
		}
		/* Tx clock routing */
		if (uf_info->tx_clock != QE_CLK_NONE) {
			if (ucc_set_qe_mux_rxtx
			    (uf_info->ucc_num, uf_info->tx_clock,
			     COMM_DIR_TX)) {
				uccf_err
		("ucc_fast_init: Illegal value for parameter 'TxClock'.");
				ucc_fast_free(uccf);
				return -EINVAL;
			}
		}
	}

	/* Set interrupt mask register at UCC level. */
	out_be32(&uf_regs->uccm, uf_info->uccm_mask);

	/* First, clear anything pending at UCC level,
	 * otherwise, old garbage may come through
	 * as soon as the dam is opened
	 * Writing '1' clears
	 */
	out_be32(&uf_regs->ucce, 0xffffffff);

	*uccf_ret = uccf;
	return 0;
}

void ucc_fast_free(struct ucc_fast_private * uccf)
{
	if (!uccf)
		return;

	if (uccf->ucc_fast_tx_virtual_fifo_base_offset)
		qe_muram_free(uccf->ucc_fast_tx_virtual_fifo_base_offset);

	if (uccf->ucc_fast_rx_virtual_fifo_base_offset)
		qe_muram_free(uccf->ucc_fast_rx_virtual_fifo_base_offset);

	kfree(uccf);
}
