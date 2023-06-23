// SPDX-License-Identifier: GPL-2.0-only
/*
 * Bestcomm ATA task driver
 *
 * Patterned after bestcomm/fec.c by Dale Farnsworth <dfarnsworth@mvista.com>
 *                                   2003-2004 (c) MontaVista, Software, Inc.
 *
 * Copyright (C) 2006-2007 Sylvain Munaut <tnt@246tNt.com>
 * Copyright (C) 2006      Freescale - John Rigby
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <asm/io.h>

#include <linux/fsl/bestcomm/bestcomm.h>
#include <linux/fsl/bestcomm/bestcomm_priv.h>
#include <linux/fsl/bestcomm/ata.h>


/* ======================================================================== */
/* Task image/var/inc                                                       */
/* ======================================================================== */

/* ata task image */
extern u32 bcom_ata_task[];

/* ata task vars that need to be set before enabling the task */
struct bcom_ata_var {
	u32 enable;		/* (u16*) address of task's control register */
	u32 bd_base;		/* (struct bcom_bd*) beginning of ring buffer */
	u32 bd_last;		/* (struct bcom_bd*) end of ring buffer */
	u32 bd_start;		/* (struct bcom_bd*) current bd */
	u32 buffer_size;	/* size of receive buffer */
};

/* ata task incs that need to be set before enabling the task */
struct bcom_ata_inc {
	u16 pad0;
	s16 incr_bytes;
	u16 pad1;
	s16 incr_dst;
	u16 pad2;
	s16 incr_src;
};


/* ======================================================================== */
/* Task support code                                                        */
/* ======================================================================== */

struct bcom_task *
bcom_ata_init(int queue_len, int maxbufsize)
{
	struct bcom_task *tsk;
	struct bcom_ata_var *var;
	struct bcom_ata_inc *inc;

	/* Prefetch breaks ATA DMA.  Turn it off for ATA DMA */
	bcom_disable_prefetch();

	tsk = bcom_task_alloc(queue_len, sizeof(struct bcom_ata_bd), 0);
	if (!tsk)
		return NULL;

	tsk->flags = BCOM_FLAGS_NONE;

	bcom_ata_reset_bd(tsk);

	var = (struct bcom_ata_var *) bcom_task_var(tsk->tasknum);
	inc = (struct bcom_ata_inc *) bcom_task_inc(tsk->tasknum);

	if (bcom_load_image(tsk->tasknum, bcom_ata_task)) {
		bcom_task_free(tsk);
		return NULL;
	}

	var->enable	= bcom_eng->regs_base +
				offsetof(struct mpc52xx_sdma, tcr[tsk->tasknum]);
	var->bd_base	= tsk->bd_pa;
	var->bd_last	= tsk->bd_pa + ((tsk->num_bd-1) * tsk->bd_size);
	var->bd_start	= tsk->bd_pa;
	var->buffer_size = maxbufsize;

	/* Configure some stuff */
	bcom_set_task_pragma(tsk->tasknum, BCOM_ATA_PRAGMA);
	bcom_set_task_auto_start(tsk->tasknum, tsk->tasknum);

	out_8(&bcom_eng->regs->ipr[BCOM_INITIATOR_ATA_RX], BCOM_IPR_ATA_RX);
	out_8(&bcom_eng->regs->ipr[BCOM_INITIATOR_ATA_TX], BCOM_IPR_ATA_TX);

	out_be32(&bcom_eng->regs->IntPend, 1<<tsk->tasknum); /* Clear ints */

	return tsk;
}
EXPORT_SYMBOL_GPL(bcom_ata_init);

void bcom_ata_rx_prepare(struct bcom_task *tsk)
{
	struct bcom_ata_inc *inc;

	inc = (struct bcom_ata_inc *) bcom_task_inc(tsk->tasknum);

	inc->incr_bytes	= -(s16)sizeof(u32);
	inc->incr_src	= 0;
	inc->incr_dst	= sizeof(u32);

	bcom_set_initiator(tsk->tasknum, BCOM_INITIATOR_ATA_RX);
}
EXPORT_SYMBOL_GPL(bcom_ata_rx_prepare);

void bcom_ata_tx_prepare(struct bcom_task *tsk)
{
	struct bcom_ata_inc *inc;

	inc = (struct bcom_ata_inc *) bcom_task_inc(tsk->tasknum);

	inc->incr_bytes	= -(s16)sizeof(u32);
	inc->incr_src	= sizeof(u32);
	inc->incr_dst	= 0;

	bcom_set_initiator(tsk->tasknum, BCOM_INITIATOR_ATA_TX);
}
EXPORT_SYMBOL_GPL(bcom_ata_tx_prepare);

void bcom_ata_reset_bd(struct bcom_task *tsk)
{
	struct bcom_ata_var *var;

	/* Reset all BD */
	memset_io(tsk->bd, 0x00, tsk->num_bd * tsk->bd_size);

	tsk->index = 0;
	tsk->outdex = 0;

	var = (struct bcom_ata_var *) bcom_task_var(tsk->tasknum);
	var->bd_start = var->bd_base;
}
EXPORT_SYMBOL_GPL(bcom_ata_reset_bd);

void bcom_ata_release(struct bcom_task *tsk)
{
	/* Nothing special for the ATA tasks */
	bcom_task_free(tsk);
}
EXPORT_SYMBOL_GPL(bcom_ata_release);


MODULE_DESCRIPTION("BestComm ATA task driver");
MODULE_AUTHOR("John Rigby");
MODULE_LICENSE("GPL v2");
