// SPDX-License-Identifier: GPL-2.0-only
/*
 * Aic94xx SAS/SATA driver sequencer interface.
 *
 * Copyright (C) 2005 Adaptec, Inc.  All rights reserved.
 * Copyright (C) 2005 Luben Tuikov <luben_tuikov@adaptec.com>
 *
 * Parts of this code adapted from David Chaw's adp94xx_seq.c.
 */

#include <linux/delay.h>
#include <linux/gfp.h>
#include <linux/pci.h>
#include <linux/module.h>
#include <linux/firmware.h>
#include "aic94xx_reg.h"
#include "aic94xx_hwi.h"

#include "aic94xx_seq.h"
#include "aic94xx_dump.h"

/* It takes no more than 0.05 us for an instruction
 * to complete. So waiting for 1 us should be more than
 * plenty.
 */
#define PAUSE_DELAY 1
#define PAUSE_TRIES 1000

static const struct firmware *sequencer_fw;
static u16 cseq_vecs[CSEQ_NUM_VECS], lseq_vecs[LSEQ_NUM_VECS], mode2_task,
	cseq_idle_loop, lseq_idle_loop;
static const u8 *cseq_code, *lseq_code;
static u32 cseq_code_size, lseq_code_size;

static u16 first_scb_site_no = 0xFFFF;
static u16 last_scb_site_no;

/* ---------- Pause/Unpause CSEQ/LSEQ ---------- */

/**
 * asd_pause_cseq - pause the central sequencer
 * @asd_ha: pointer to host adapter structure
 *
 * Return 0 on success, negative on failure.
 */
static int asd_pause_cseq(struct asd_ha_struct *asd_ha)
{
	int	count = PAUSE_TRIES;
	u32	arp2ctl;

	arp2ctl = asd_read_reg_dword(asd_ha, CARP2CTL);
	if (arp2ctl & PAUSED)
		return 0;

	asd_write_reg_dword(asd_ha, CARP2CTL, arp2ctl | EPAUSE);
	do {
		arp2ctl = asd_read_reg_dword(asd_ha, CARP2CTL);
		if (arp2ctl & PAUSED)
			return 0;
		udelay(PAUSE_DELAY);
	} while (--count > 0);

	ASD_DPRINTK("couldn't pause CSEQ\n");
	return -1;
}

/**
 * asd_unpause_cseq - unpause the central sequencer.
 * @asd_ha: pointer to host adapter structure.
 *
 * Return 0 on success, negative on error.
 */
static int asd_unpause_cseq(struct asd_ha_struct *asd_ha)
{
	u32	arp2ctl;
	int	count = PAUSE_TRIES;

	arp2ctl = asd_read_reg_dword(asd_ha, CARP2CTL);
	if (!(arp2ctl & PAUSED))
		return 0;

	asd_write_reg_dword(asd_ha, CARP2CTL, arp2ctl & ~EPAUSE);
	do {
		arp2ctl = asd_read_reg_dword(asd_ha, CARP2CTL);
		if (!(arp2ctl & PAUSED))
			return 0;
		udelay(PAUSE_DELAY);
	} while (--count > 0);

	ASD_DPRINTK("couldn't unpause the CSEQ\n");
	return -1;
}

/**
 * asd_seq_pause_lseq - pause a link sequencer
 * @asd_ha: pointer to a host adapter structure
 * @lseq: link sequencer of interest
 *
 * Return 0 on success, negative on error.
 */
static int asd_seq_pause_lseq(struct asd_ha_struct *asd_ha, int lseq)
{
	u32    arp2ctl;
	int    count = PAUSE_TRIES;

	arp2ctl = asd_read_reg_dword(asd_ha, LmARP2CTL(lseq));
	if (arp2ctl & PAUSED)
		return 0;

	asd_write_reg_dword(asd_ha, LmARP2CTL(lseq), arp2ctl | EPAUSE);
	do {
		arp2ctl = asd_read_reg_dword(asd_ha, LmARP2CTL(lseq));
		if (arp2ctl & PAUSED)
			return 0;
		udelay(PAUSE_DELAY);
	} while (--count > 0);

	ASD_DPRINTK("couldn't pause LSEQ %d\n", lseq);
	return -1;
}

/**
 * asd_pause_lseq - pause the link sequencer(s)
 * @asd_ha: pointer to host adapter structure
 * @lseq_mask: mask of link sequencers of interest
 *
 * Return 0 on success, negative on failure.
 */
static int asd_pause_lseq(struct asd_ha_struct *asd_ha, u8 lseq_mask)
{
	int lseq;
	int err = 0;

	for_each_sequencer(lseq_mask, lseq_mask, lseq) {
		err = asd_seq_pause_lseq(asd_ha, lseq);
		if (err)
			return err;
	}

	return err;
}

/**
 * asd_seq_unpause_lseq - unpause a link sequencer
 * @asd_ha: pointer to host adapter structure
 * @lseq: link sequencer of interest
 *
 * Return 0 on success, negative on error.
 */
static int asd_seq_unpause_lseq(struct asd_ha_struct *asd_ha, int lseq)
{
	u32 arp2ctl;
	int count = PAUSE_TRIES;

	arp2ctl = asd_read_reg_dword(asd_ha, LmARP2CTL(lseq));
	if (!(arp2ctl & PAUSED))
		return 0;

	asd_write_reg_dword(asd_ha, LmARP2CTL(lseq), arp2ctl & ~EPAUSE);
	do {
		arp2ctl = asd_read_reg_dword(asd_ha, LmARP2CTL(lseq));
		if (!(arp2ctl & PAUSED))
			return 0;
		udelay(PAUSE_DELAY);
	} while (--count > 0);

	ASD_DPRINTK("couldn't unpause LSEQ %d\n", lseq);
	return 0;
}


/* ---------- Downloading CSEQ/LSEQ microcode ---------- */

static int asd_verify_cseq(struct asd_ha_struct *asd_ha, const u8 *_prog,
			   u32 size)
{
	u32 addr = CSEQ_RAM_REG_BASE_ADR;
	const u32 *prog = (u32 *) _prog;
	u32 i;

	for (i = 0; i < size; i += 4, prog++, addr += 4) {
		u32 val = asd_read_reg_dword(asd_ha, addr);

		if (le32_to_cpu(*prog) != val) {
			asd_printk("%s: cseq verify failed at %u "
				   "read:0x%x, wanted:0x%x\n",
				   pci_name(asd_ha->pcidev),
				   i, val, le32_to_cpu(*prog));
			return -1;
		}
	}
	ASD_DPRINTK("verified %d bytes, passed\n", size);
	return 0;
}

/**
 * asd_verify_lseq - verify the microcode of a link sequencer
 * @asd_ha: pointer to host adapter structure
 * @_prog: pointer to the microcode
 * @size: size of the microcode in bytes
 * @lseq: link sequencer of interest
 *
 * The link sequencer code is accessed in 4 KB pages, which are selected
 * by setting LmRAMPAGE (bits 8 and 9) of the LmBISTCTL1 register.
 * The 10 KB LSEQm instruction code is mapped, page at a time, at
 * LmSEQRAM address.
 */
static int asd_verify_lseq(struct asd_ha_struct *asd_ha, const u8 *_prog,
			   u32 size, int lseq)
{
#define LSEQ_CODEPAGE_SIZE 4096
	int pages =  (size + LSEQ_CODEPAGE_SIZE - 1) / LSEQ_CODEPAGE_SIZE;
	u32 page;
	const u32 *prog = (u32 *) _prog;

	for (page = 0; page < pages; page++) {
		u32 i;

		asd_write_reg_dword(asd_ha, LmBISTCTL1(lseq),
				    page << LmRAMPAGE_LSHIFT);
		for (i = 0; size > 0 && i < LSEQ_CODEPAGE_SIZE;
		     i += 4, prog++, size-=4) {

			u32 val = asd_read_reg_dword(asd_ha, LmSEQRAM(lseq)+i);

			if (le32_to_cpu(*prog) != val) {
				asd_printk("%s: LSEQ%d verify failed "
					   "page:%d, offs:%d\n",
					   pci_name(asd_ha->pcidev),
					   lseq, page, i);
				return -1;
			}
		}
	}
	ASD_DPRINTK("LSEQ%d verified %d bytes, passed\n", lseq,
		    (int)((u8 *)prog-_prog));
	return 0;
}

/**
 * asd_verify_seq -- verify CSEQ/LSEQ microcode
 * @asd_ha: pointer to host adapter structure
 * @prog: pointer to microcode
 * @size: size of the microcode
 * @lseq_mask: if 0, verify CSEQ microcode, else mask of LSEQs of interest
 *
 * Return 0 if microcode is correct, negative on mismatch.
 */
static int asd_verify_seq(struct asd_ha_struct *asd_ha, const u8 *prog,
			      u32 size, u8 lseq_mask)
{
	if (lseq_mask == 0)
		return asd_verify_cseq(asd_ha, prog, size);
	else {
		int lseq, err;

		for_each_sequencer(lseq_mask, lseq_mask, lseq) {
			err = asd_verify_lseq(asd_ha, prog, size, lseq);
			if (err)
				return err;
		}
	}

	return 0;
}
#define ASD_DMA_MODE_DOWNLOAD
#ifdef ASD_DMA_MODE_DOWNLOAD
/* This is the size of the CSEQ Mapped instruction page */
#define MAX_DMA_OVLY_COUNT ((1U << 14)-1)
static int asd_download_seq(struct asd_ha_struct *asd_ha,
			    const u8 * const prog, u32 size, u8 lseq_mask)
{
	u32 comstaten;
	u32 reg;
	int page;
	const int pages = (size + MAX_DMA_OVLY_COUNT - 1) / MAX_DMA_OVLY_COUNT;
	struct asd_dma_tok *token;
	int err = 0;

	if (size % 4) {
		asd_printk("sequencer program not multiple of 4\n");
		return -1;
	}

	asd_pause_cseq(asd_ha);
	asd_pause_lseq(asd_ha, 0xFF);

	/* save, disable and clear interrupts */
	comstaten = asd_read_reg_dword(asd_ha, COMSTATEN);
	asd_write_reg_dword(asd_ha, COMSTATEN, 0);
	asd_write_reg_dword(asd_ha, COMSTAT, COMSTAT_MASK);

	asd_write_reg_dword(asd_ha, CHIMINTEN, RST_CHIMINTEN);
	asd_write_reg_dword(asd_ha, CHIMINT, CHIMINT_MASK);

	token = asd_alloc_coherent(asd_ha, MAX_DMA_OVLY_COUNT, GFP_KERNEL);
	if (!token) {
		asd_printk("out of memory for dma SEQ download\n");
		err = -ENOMEM;
		goto out;
	}
	ASD_DPRINTK("dma-ing %d bytes\n", size);

	for (page = 0; page < pages; page++) {
		int i;
		u32 left = min(size-page*MAX_DMA_OVLY_COUNT,
			       (u32)MAX_DMA_OVLY_COUNT);

		memcpy(token->vaddr, prog + page*MAX_DMA_OVLY_COUNT, left);
		asd_write_reg_addr(asd_ha, OVLYDMAADR, token->dma_handle);
		asd_write_reg_dword(asd_ha, OVLYDMACNT, left);
		reg = !page ? RESETOVLYDMA : 0;
		reg |= (STARTOVLYDMA | OVLYHALTERR);
		reg |= (lseq_mask ? (((u32)lseq_mask) << 8) : OVLYCSEQ);
		/* Start DMA. */
		asd_write_reg_dword(asd_ha, OVLYDMACTL, reg);

		for (i = PAUSE_TRIES*100; i > 0; i--) {
			u32 dmadone = asd_read_reg_dword(asd_ha, OVLYDMACTL);
			if (!(dmadone & OVLYDMAACT))
				break;
			udelay(PAUSE_DELAY);
		}
	}

	reg = asd_read_reg_dword(asd_ha, COMSTAT);
	if (!(reg & OVLYDMADONE) || (reg & OVLYERR)
	    || (asd_read_reg_dword(asd_ha, CHIMINT) & DEVEXCEPT_MASK)){
		asd_printk("%s: error DMA-ing sequencer code\n",
			   pci_name(asd_ha->pcidev));
		err = -ENODEV;
	}

	asd_free_coherent(asd_ha, token);
 out:
	asd_write_reg_dword(asd_ha, COMSTATEN, comstaten);

	return err ? : asd_verify_seq(asd_ha, prog, size, lseq_mask);
}
#else /* ASD_DMA_MODE_DOWNLOAD */
static int asd_download_seq(struct asd_ha_struct *asd_ha, const u8 *_prog,
			    u32 size, u8 lseq_mask)
{
	int i;
	u32 reg = 0;
	const u32 *prog = (u32 *) _prog;

	if (size % 4) {
		asd_printk("sequencer program not multiple of 4\n");
		return -1;
	}

	asd_pause_cseq(asd_ha);
	asd_pause_lseq(asd_ha, 0xFF);

	reg |= (lseq_mask ? (((u32)lseq_mask) << 8) : OVLYCSEQ);
	reg |= PIOCMODE;

	asd_write_reg_dword(asd_ha, OVLYDMACNT, size);
	asd_write_reg_dword(asd_ha, OVLYDMACTL, reg);

	ASD_DPRINTK("downloading %s sequencer%s in PIO mode...\n",
		    lseq_mask ? "LSEQ" : "CSEQ", lseq_mask ? "s" : "");

	for (i = 0; i < size; i += 4, prog++)
		asd_write_reg_dword(asd_ha, SPIODATA, *prog);

	reg = (reg & ~PIOCMODE) | OVLYHALTERR;
	asd_write_reg_dword(asd_ha, OVLYDMACTL, reg);

	return asd_verify_seq(asd_ha, _prog, size, lseq_mask);
}
#endif /* ASD_DMA_MODE_DOWNLOAD */

/**
 * asd_seq_download_seqs - download the sequencer microcode
 * @asd_ha: pointer to host adapter structure
 *
 * Download the central and link sequencer microcode.
 */
static int asd_seq_download_seqs(struct asd_ha_struct *asd_ha)
{
	int 	err;

	if (!asd_ha->hw_prof.enabled_phys) {
		asd_printk("%s: no enabled phys!\n", pci_name(asd_ha->pcidev));
		return -ENODEV;
	}

	/* Download the CSEQ */
	ASD_DPRINTK("downloading CSEQ...\n");
	err = asd_download_seq(asd_ha, cseq_code, cseq_code_size, 0);
	if (err) {
		asd_printk("CSEQ download failed:%d\n", err);
		return err;
	}

	/* Download the Link Sequencers code. All of the Link Sequencers
	 * microcode can be downloaded at the same time.
	 */
	ASD_DPRINTK("downloading LSEQs...\n");
	err = asd_download_seq(asd_ha, lseq_code, lseq_code_size,
			       asd_ha->hw_prof.enabled_phys);
	if (err) {
		/* Try it one at a time */
		u8 lseq;
		u8 lseq_mask = asd_ha->hw_prof.enabled_phys;

		for_each_sequencer(lseq_mask, lseq_mask, lseq) {
			err = asd_download_seq(asd_ha, lseq_code,
					       lseq_code_size, 1<<lseq);
			if (err)
				break;
		}
	}
	if (err)
		asd_printk("LSEQs download failed:%d\n", err);

	return err;
}

/* ---------- Initializing the chip, chip memory, etc. ---------- */

/**
 * asd_init_cseq_mip - initialize CSEQ mode independent pages 4-7
 * @asd_ha: pointer to host adapter structure
 */
static void asd_init_cseq_mip(struct asd_ha_struct *asd_ha)
{
	/* CSEQ Mode Independent, page 4 setup. */
	asd_write_reg_word(asd_ha, CSEQ_Q_EXE_HEAD, 0xFFFF);
	asd_write_reg_word(asd_ha, CSEQ_Q_EXE_TAIL, 0xFFFF);
	asd_write_reg_word(asd_ha, CSEQ_Q_DONE_HEAD, 0xFFFF);
	asd_write_reg_word(asd_ha, CSEQ_Q_DONE_TAIL, 0xFFFF);
	asd_write_reg_word(asd_ha, CSEQ_Q_SEND_HEAD, 0xFFFF);
	asd_write_reg_word(asd_ha, CSEQ_Q_SEND_TAIL, 0xFFFF);
	asd_write_reg_word(asd_ha, CSEQ_Q_DMA2CHIM_HEAD, 0xFFFF);
	asd_write_reg_word(asd_ha, CSEQ_Q_DMA2CHIM_TAIL, 0xFFFF);
	asd_write_reg_word(asd_ha, CSEQ_Q_COPY_HEAD, 0xFFFF);
	asd_write_reg_word(asd_ha, CSEQ_Q_COPY_TAIL, 0xFFFF);
	asd_write_reg_word(asd_ha, CSEQ_REG0, 0);
	asd_write_reg_word(asd_ha, CSEQ_REG1, 0);
	asd_write_reg_dword(asd_ha, CSEQ_REG2, 0);
	asd_write_reg_byte(asd_ha, CSEQ_LINK_CTL_Q_MAP, 0);
	{
		u8 con = asd_read_reg_byte(asd_ha, CCONEXIST);
		u8 val = hweight8(con);
		asd_write_reg_byte(asd_ha, CSEQ_MAX_CSEQ_MODE, (val<<4)|val);
	}
	asd_write_reg_word(asd_ha, CSEQ_FREE_LIST_HACK_COUNT, 0);

	/* CSEQ Mode independent, page 5 setup. */
	asd_write_reg_dword(asd_ha, CSEQ_EST_NEXUS_REQ_QUEUE, 0);
	asd_write_reg_dword(asd_ha, CSEQ_EST_NEXUS_REQ_QUEUE+4, 0);
	asd_write_reg_dword(asd_ha, CSEQ_EST_NEXUS_REQ_COUNT, 0);
	asd_write_reg_dword(asd_ha, CSEQ_EST_NEXUS_REQ_COUNT+4, 0);
	asd_write_reg_word(asd_ha, CSEQ_Q_EST_NEXUS_HEAD, 0xFFFF);
	asd_write_reg_word(asd_ha, CSEQ_Q_EST_NEXUS_TAIL, 0xFFFF);
	asd_write_reg_word(asd_ha, CSEQ_NEED_EST_NEXUS_SCB, 0);
	asd_write_reg_byte(asd_ha, CSEQ_EST_NEXUS_REQ_HEAD, 0);
	asd_write_reg_byte(asd_ha, CSEQ_EST_NEXUS_REQ_TAIL, 0);
	asd_write_reg_byte(asd_ha, CSEQ_EST_NEXUS_SCB_OFFSET, 0);

	/* CSEQ Mode independent, page 6 setup. */
	asd_write_reg_word(asd_ha, CSEQ_INT_ROUT_RET_ADDR0, 0);
	asd_write_reg_word(asd_ha, CSEQ_INT_ROUT_RET_ADDR1, 0);
	asd_write_reg_word(asd_ha, CSEQ_INT_ROUT_SCBPTR, 0);
	asd_write_reg_byte(asd_ha, CSEQ_INT_ROUT_MODE, 0);
	asd_write_reg_byte(asd_ha, CSEQ_ISR_SCRATCH_FLAGS, 0);
	asd_write_reg_word(asd_ha, CSEQ_ISR_SAVE_SINDEX, 0);
	asd_write_reg_word(asd_ha, CSEQ_ISR_SAVE_DINDEX, 0);
	asd_write_reg_word(asd_ha, CSEQ_Q_MONIRTT_HEAD, 0xFFFF);
	asd_write_reg_word(asd_ha, CSEQ_Q_MONIRTT_TAIL, 0xFFFF);
	/* Calculate the free scb mask. */
	{
		u16 cmdctx = asd_get_cmdctx_size(asd_ha);
		cmdctx = (~((cmdctx/128)-1)) >> 8;
		asd_write_reg_byte(asd_ha, CSEQ_FREE_SCB_MASK, (u8)cmdctx);
	}
	asd_write_reg_word(asd_ha, CSEQ_BUILTIN_FREE_SCB_HEAD,
			   first_scb_site_no);
	asd_write_reg_word(asd_ha, CSEQ_BUILTIN_FREE_SCB_TAIL,
			   last_scb_site_no);
	asd_write_reg_word(asd_ha, CSEQ_EXTENDED_FREE_SCB_HEAD, 0xFFFF);
	asd_write_reg_word(asd_ha, CSEQ_EXTENDED_FREE_SCB_TAIL, 0xFFFF);

	/* CSEQ Mode independent, page 7 setup. */
	asd_write_reg_dword(asd_ha, CSEQ_EMPTY_REQ_QUEUE, 0);
	asd_write_reg_dword(asd_ha, CSEQ_EMPTY_REQ_QUEUE+4, 0);
	asd_write_reg_dword(asd_ha, CSEQ_EMPTY_REQ_COUNT, 0);
	asd_write_reg_dword(asd_ha, CSEQ_EMPTY_REQ_COUNT+4, 0);
	asd_write_reg_word(asd_ha, CSEQ_Q_EMPTY_HEAD, 0xFFFF);
	asd_write_reg_word(asd_ha, CSEQ_Q_EMPTY_TAIL, 0xFFFF);
	asd_write_reg_word(asd_ha, CSEQ_NEED_EMPTY_SCB, 0);
	asd_write_reg_byte(asd_ha, CSEQ_EMPTY_REQ_HEAD, 0);
	asd_write_reg_byte(asd_ha, CSEQ_EMPTY_REQ_TAIL, 0);
	asd_write_reg_byte(asd_ha, CSEQ_EMPTY_SCB_OFFSET, 0);
	asd_write_reg_word(asd_ha, CSEQ_PRIMITIVE_DATA, 0);
	asd_write_reg_dword(asd_ha, CSEQ_TIMEOUT_CONST, 0);
}

/**
 * asd_init_cseq_mdp - initialize CSEQ Mode dependent pages
 * @asd_ha: pointer to host adapter structure
 */
static void asd_init_cseq_mdp(struct asd_ha_struct *asd_ha)
{
	int	i;
	int	moffs;

	moffs = CSEQ_PAGE_SIZE * 2;

	/* CSEQ Mode dependent, modes 0-7, page 0 setup. */
	for (i = 0; i < 8; i++) {
		asd_write_reg_word(asd_ha, i*moffs+CSEQ_LRM_SAVE_SINDEX, 0);
		asd_write_reg_word(asd_ha, i*moffs+CSEQ_LRM_SAVE_SCBPTR, 0);
		asd_write_reg_word(asd_ha, i*moffs+CSEQ_Q_LINK_HEAD, 0xFFFF);
		asd_write_reg_word(asd_ha, i*moffs+CSEQ_Q_LINK_TAIL, 0xFFFF);
		asd_write_reg_byte(asd_ha, i*moffs+CSEQ_LRM_SAVE_SCRPAGE, 0);
	}

	/* CSEQ Mode dependent, mode 0-7, page 1 and 2 shall be ignored. */

	/* CSEQ Mode dependent, mode 8, page 0 setup. */
	asd_write_reg_word(asd_ha, CSEQ_RET_ADDR, 0xFFFF);
	asd_write_reg_word(asd_ha, CSEQ_RET_SCBPTR, 0);
	asd_write_reg_word(asd_ha, CSEQ_SAVE_SCBPTR, 0);
	asd_write_reg_word(asd_ha, CSEQ_EMPTY_TRANS_CTX, 0);
	asd_write_reg_word(asd_ha, CSEQ_RESP_LEN, 0);
	asd_write_reg_word(asd_ha, CSEQ_TMF_SCBPTR, 0);
	asd_write_reg_word(asd_ha, CSEQ_GLOBAL_PREV_SCB, 0);
	asd_write_reg_word(asd_ha, CSEQ_GLOBAL_HEAD, 0);
	asd_write_reg_word(asd_ha, CSEQ_CLEAR_LU_HEAD, 0);
	asd_write_reg_byte(asd_ha, CSEQ_TMF_OPCODE, 0);
	asd_write_reg_byte(asd_ha, CSEQ_SCRATCH_FLAGS, 0);
	asd_write_reg_word(asd_ha, CSEQ_HSB_SITE, 0);
	asd_write_reg_word(asd_ha, CSEQ_FIRST_INV_SCB_SITE,
			   (u16)last_scb_site_no+1);
	asd_write_reg_word(asd_ha, CSEQ_FIRST_INV_DDB_SITE,
			   (u16)asd_ha->hw_prof.max_ddbs);

	/* CSEQ Mode dependent, mode 8, page 1 setup. */
	asd_write_reg_dword(asd_ha, CSEQ_LUN_TO_CLEAR, 0);
	asd_write_reg_dword(asd_ha, CSEQ_LUN_TO_CLEAR + 4, 0);
	asd_write_reg_dword(asd_ha, CSEQ_LUN_TO_CHECK, 0);
	asd_write_reg_dword(asd_ha, CSEQ_LUN_TO_CHECK + 4, 0);

	/* CSEQ Mode dependent, mode 8, page 2 setup. */
	/* Tell the sequencer the bus address of the first SCB. */
	asd_write_reg_addr(asd_ha, CSEQ_HQ_NEW_POINTER,
			   asd_ha->seq.next_scb.dma_handle);
	ASD_DPRINTK("First SCB dma_handle: 0x%llx\n",
		    (unsigned long long)asd_ha->seq.next_scb.dma_handle);

	/* Tell the sequencer the first Done List entry address. */
	asd_write_reg_addr(asd_ha, CSEQ_HQ_DONE_BASE,
			   asd_ha->seq.actual_dl->dma_handle);

	/* Initialize the Q_DONE_POINTER with the least significant
	 * 4 bytes of the first Done List address. */
	asd_write_reg_dword(asd_ha, CSEQ_HQ_DONE_POINTER,
			    ASD_BUSADDR_LO(asd_ha->seq.actual_dl->dma_handle));

	asd_write_reg_byte(asd_ha, CSEQ_HQ_DONE_PASS, ASD_DEF_DL_TOGGLE);

	/* CSEQ Mode dependent, mode 8, page 3 shall be ignored. */
}

/**
 * asd_init_cseq_scratch -- setup and init CSEQ
 * @asd_ha: pointer to host adapter structure
 *
 * Setup and initialize Central sequencers. Initialize the mode
 * independent and dependent scratch page to the default settings.
 */
static void asd_init_cseq_scratch(struct asd_ha_struct *asd_ha)
{
	asd_init_cseq_mip(asd_ha);
	asd_init_cseq_mdp(asd_ha);
}

/**
 * asd_init_lseq_mip -- initialize LSEQ Mode independent pages 0-3
 * @asd_ha: pointer to host adapter structure
 * @lseq:  link sequencer
 */
static void asd_init_lseq_mip(struct asd_ha_struct *asd_ha, u8 lseq)
{
	int i;

	/* LSEQ Mode independent page 0 setup. */
	asd_write_reg_word(asd_ha, LmSEQ_Q_TGTXFR_HEAD(lseq), 0xFFFF);
	asd_write_reg_word(asd_ha, LmSEQ_Q_TGTXFR_TAIL(lseq), 0xFFFF);
	asd_write_reg_byte(asd_ha, LmSEQ_LINK_NUMBER(lseq), lseq);
	asd_write_reg_byte(asd_ha, LmSEQ_SCRATCH_FLAGS(lseq),
			   ASD_NOTIFY_ENABLE_SPINUP);
	asd_write_reg_dword(asd_ha, LmSEQ_CONNECTION_STATE(lseq),0x08000000);
	asd_write_reg_word(asd_ha, LmSEQ_CONCTL(lseq), 0);
	asd_write_reg_byte(asd_ha, LmSEQ_CONSTAT(lseq), 0);
	asd_write_reg_byte(asd_ha, LmSEQ_CONNECTION_MODES(lseq), 0);
	asd_write_reg_word(asd_ha, LmSEQ_REG1_ISR(lseq), 0);
	asd_write_reg_word(asd_ha, LmSEQ_REG2_ISR(lseq), 0);
	asd_write_reg_word(asd_ha, LmSEQ_REG3_ISR(lseq), 0);
	asd_write_reg_dword(asd_ha, LmSEQ_REG0_ISR(lseq), 0);
	asd_write_reg_dword(asd_ha, LmSEQ_REG0_ISR(lseq)+4, 0);

	/* LSEQ Mode independent page 1 setup. */
	asd_write_reg_word(asd_ha, LmSEQ_EST_NEXUS_SCBPTR0(lseq), 0xFFFF);
	asd_write_reg_word(asd_ha, LmSEQ_EST_NEXUS_SCBPTR1(lseq), 0xFFFF);
	asd_write_reg_word(asd_ha, LmSEQ_EST_NEXUS_SCBPTR2(lseq), 0xFFFF);
	asd_write_reg_word(asd_ha, LmSEQ_EST_NEXUS_SCBPTR3(lseq), 0xFFFF);
	asd_write_reg_byte(asd_ha, LmSEQ_EST_NEXUS_SCB_OPCODE0(lseq), 0);
	asd_write_reg_byte(asd_ha, LmSEQ_EST_NEXUS_SCB_OPCODE1(lseq), 0);
	asd_write_reg_byte(asd_ha, LmSEQ_EST_NEXUS_SCB_OPCODE2(lseq), 0);
	asd_write_reg_byte(asd_ha, LmSEQ_EST_NEXUS_SCB_OPCODE3(lseq), 0);
	asd_write_reg_byte(asd_ha, LmSEQ_EST_NEXUS_SCB_HEAD(lseq), 0);
	asd_write_reg_byte(asd_ha, LmSEQ_EST_NEXUS_SCB_TAIL(lseq), 0);
	asd_write_reg_byte(asd_ha, LmSEQ_EST_NEXUS_BUF_AVAIL(lseq), 0);
	asd_write_reg_dword(asd_ha, LmSEQ_TIMEOUT_CONST(lseq), 0);
	asd_write_reg_word(asd_ha, LmSEQ_ISR_SAVE_SINDEX(lseq), 0);
	asd_write_reg_word(asd_ha, LmSEQ_ISR_SAVE_DINDEX(lseq), 0);

	/* LSEQ Mode Independent page 2 setup. */
	asd_write_reg_word(asd_ha, LmSEQ_EMPTY_SCB_PTR0(lseq), 0xFFFF);
	asd_write_reg_word(asd_ha, LmSEQ_EMPTY_SCB_PTR1(lseq), 0xFFFF);
	asd_write_reg_word(asd_ha, LmSEQ_EMPTY_SCB_PTR2(lseq), 0xFFFF);
	asd_write_reg_word(asd_ha, LmSEQ_EMPTY_SCB_PTR3(lseq), 0xFFFF);
	asd_write_reg_byte(asd_ha, LmSEQ_EMPTY_SCB_OPCD0(lseq), 0);
	asd_write_reg_byte(asd_ha, LmSEQ_EMPTY_SCB_OPCD1(lseq), 0);
	asd_write_reg_byte(asd_ha, LmSEQ_EMPTY_SCB_OPCD2(lseq), 0);
	asd_write_reg_byte(asd_ha, LmSEQ_EMPTY_SCB_OPCD3(lseq), 0);
	asd_write_reg_byte(asd_ha, LmSEQ_EMPTY_SCB_HEAD(lseq), 0);
	asd_write_reg_byte(asd_ha, LmSEQ_EMPTY_SCB_TAIL(lseq), 0);
	asd_write_reg_byte(asd_ha, LmSEQ_EMPTY_BUFS_AVAIL(lseq), 0);
	for (i = 0; i < 12; i += 4)
		asd_write_reg_dword(asd_ha, LmSEQ_ATA_SCR_REGS(lseq) + i, 0);

	/* LSEQ Mode Independent page 3 setup. */

	/* Device present timer timeout */
	asd_write_reg_dword(asd_ha, LmSEQ_DEV_PRES_TMR_TOUT_CONST(lseq),
			    ASD_DEV_PRESENT_TIMEOUT);

	/* SATA interlock timer disabled */
	asd_write_reg_dword(asd_ha, LmSEQ_SATA_INTERLOCK_TIMEOUT(lseq),
			    ASD_SATA_INTERLOCK_TIMEOUT);

	/* STP shutdown timer timeout constant, IGNORED by the sequencer,
	 * always 0. */
	asd_write_reg_dword(asd_ha, LmSEQ_STP_SHUTDOWN_TIMEOUT(lseq),
			    ASD_STP_SHUTDOWN_TIMEOUT);

	asd_write_reg_dword(asd_ha, LmSEQ_SRST_ASSERT_TIMEOUT(lseq),
			    ASD_SRST_ASSERT_TIMEOUT);

	asd_write_reg_dword(asd_ha, LmSEQ_RCV_FIS_TIMEOUT(lseq),
			    ASD_RCV_FIS_TIMEOUT);

	asd_write_reg_dword(asd_ha, LmSEQ_ONE_MILLISEC_TIMEOUT(lseq),
			    ASD_ONE_MILLISEC_TIMEOUT);

	/* COM_INIT timer */
	asd_write_reg_dword(asd_ha, LmSEQ_TEN_MS_COMINIT_TIMEOUT(lseq),
			    ASD_TEN_MILLISEC_TIMEOUT);

	asd_write_reg_dword(asd_ha, LmSEQ_SMP_RCV_TIMEOUT(lseq),
			    ASD_SMP_RCV_TIMEOUT);
}

/**
 * asd_init_lseq_mdp -- initialize LSEQ mode dependent pages.
 * @asd_ha: pointer to host adapter structure
 * @lseq:  link sequencer
 */
static void asd_init_lseq_mdp(struct asd_ha_struct *asd_ha,  int lseq)
{
	int    i;
	u32    moffs;
	u16 ret_addr[] = {
		0xFFFF,		  /* mode 0 */
		0xFFFF,		  /* mode 1 */
		mode2_task,	  /* mode 2 */
		0,
		0xFFFF,		  /* mode 4/5 */
		0xFFFF,		  /* mode 4/5 */
	};

	/*
	 * Mode 0,1,2 and 4/5 have common field on page 0 for the first
	 * 14 bytes.
	 */
	for (i = 0; i < 3; i++) {
		moffs = i * LSEQ_MODE_SCRATCH_SIZE;
		asd_write_reg_word(asd_ha, LmSEQ_RET_ADDR(lseq)+moffs,
				   ret_addr[i]);
		asd_write_reg_word(asd_ha, LmSEQ_REG0_MODE(lseq)+moffs, 0);
		asd_write_reg_word(asd_ha, LmSEQ_MODE_FLAGS(lseq)+moffs, 0);
		asd_write_reg_word(asd_ha, LmSEQ_RET_ADDR2(lseq)+moffs,0xFFFF);
		asd_write_reg_word(asd_ha, LmSEQ_RET_ADDR1(lseq)+moffs,0xFFFF);
		asd_write_reg_byte(asd_ha, LmSEQ_OPCODE_TO_CSEQ(lseq)+moffs,0);
		asd_write_reg_word(asd_ha, LmSEQ_DATA_TO_CSEQ(lseq)+moffs,0);
	}
	/*
	 *  Mode 5 page 0 overlaps the same scratch page with Mode 0 page 3.
	 */
	asd_write_reg_word(asd_ha,
			 LmSEQ_RET_ADDR(lseq)+LSEQ_MODE5_PAGE0_OFFSET,
			   ret_addr[5]);
	asd_write_reg_word(asd_ha,
			 LmSEQ_REG0_MODE(lseq)+LSEQ_MODE5_PAGE0_OFFSET,0);
	asd_write_reg_word(asd_ha,
			 LmSEQ_MODE_FLAGS(lseq)+LSEQ_MODE5_PAGE0_OFFSET, 0);
	asd_write_reg_word(asd_ha,
			 LmSEQ_RET_ADDR2(lseq)+LSEQ_MODE5_PAGE0_OFFSET,0xFFFF);
	asd_write_reg_word(asd_ha,
			 LmSEQ_RET_ADDR1(lseq)+LSEQ_MODE5_PAGE0_OFFSET,0xFFFF);
	asd_write_reg_byte(asd_ha,
		         LmSEQ_OPCODE_TO_CSEQ(lseq)+LSEQ_MODE5_PAGE0_OFFSET,0);
	asd_write_reg_word(asd_ha,
		         LmSEQ_DATA_TO_CSEQ(lseq)+LSEQ_MODE5_PAGE0_OFFSET, 0);

	/* LSEQ Mode dependent 0, page 0 setup. */
	asd_write_reg_word(asd_ha, LmSEQ_FIRST_INV_DDB_SITE(lseq),
			   (u16)asd_ha->hw_prof.max_ddbs);
	asd_write_reg_word(asd_ha, LmSEQ_EMPTY_TRANS_CTX(lseq), 0);
	asd_write_reg_word(asd_ha, LmSEQ_RESP_LEN(lseq), 0);
	asd_write_reg_word(asd_ha, LmSEQ_FIRST_INV_SCB_SITE(lseq),
			   (u16)last_scb_site_no+1);
	asd_write_reg_word(asd_ha, LmSEQ_INTEN_SAVE(lseq),
			    (u16) ((LmM0INTEN_MASK & 0xFFFF0000) >> 16));
	asd_write_reg_word(asd_ha, LmSEQ_INTEN_SAVE(lseq) + 2,
			    (u16) LmM0INTEN_MASK & 0xFFFF);
	asd_write_reg_byte(asd_ha, LmSEQ_LINK_RST_FRM_LEN(lseq), 0);
	asd_write_reg_byte(asd_ha, LmSEQ_LINK_RST_PROTOCOL(lseq), 0);
	asd_write_reg_byte(asd_ha, LmSEQ_RESP_STATUS(lseq), 0);
	asd_write_reg_byte(asd_ha, LmSEQ_LAST_LOADED_SGE(lseq), 0);
	asd_write_reg_word(asd_ha, LmSEQ_SAVE_SCBPTR(lseq), 0);

	/* LSEQ mode dependent, mode 1, page 0 setup. */
	asd_write_reg_word(asd_ha, LmSEQ_Q_XMIT_HEAD(lseq), 0xFFFF);
	asd_write_reg_word(asd_ha, LmSEQ_M1_EMPTY_TRANS_CTX(lseq), 0);
	asd_write_reg_word(asd_ha, LmSEQ_INI_CONN_TAG(lseq), 0);
	asd_write_reg_byte(asd_ha, LmSEQ_FAILED_OPEN_STATUS(lseq), 0);
	asd_write_reg_byte(asd_ha, LmSEQ_XMIT_REQUEST_TYPE(lseq), 0);
	asd_write_reg_byte(asd_ha, LmSEQ_M1_RESP_STATUS(lseq), 0);
	asd_write_reg_byte(asd_ha, LmSEQ_M1_LAST_LOADED_SGE(lseq), 0);
	asd_write_reg_word(asd_ha, LmSEQ_M1_SAVE_SCBPTR(lseq), 0);

	/* LSEQ Mode dependent mode 2, page 0 setup */
	asd_write_reg_word(asd_ha, LmSEQ_PORT_COUNTER(lseq), 0);
	asd_write_reg_word(asd_ha, LmSEQ_PM_TABLE_PTR(lseq), 0);
	asd_write_reg_word(asd_ha, LmSEQ_SATA_INTERLOCK_TMR_SAVE(lseq), 0);
	asd_write_reg_word(asd_ha, LmSEQ_IP_BITL(lseq), 0);
	asd_write_reg_word(asd_ha, LmSEQ_COPY_SMP_CONN_TAG(lseq), 0);
	asd_write_reg_byte(asd_ha, LmSEQ_P0M2_OFFS1AH(lseq), 0);

	/* LSEQ Mode dependent, mode 4/5, page 0 setup. */
	asd_write_reg_byte(asd_ha, LmSEQ_SAVED_OOB_STATUS(lseq), 0);
	asd_write_reg_byte(asd_ha, LmSEQ_SAVED_OOB_MODE(lseq), 0);
	asd_write_reg_word(asd_ha, LmSEQ_Q_LINK_HEAD(lseq), 0xFFFF);
	asd_write_reg_byte(asd_ha, LmSEQ_LINK_RST_ERR(lseq), 0);
	asd_write_reg_byte(asd_ha, LmSEQ_SAVED_OOB_SIGNALS(lseq), 0);
	asd_write_reg_byte(asd_ha, LmSEQ_SAS_RESET_MODE(lseq), 0);
	asd_write_reg_byte(asd_ha, LmSEQ_LINK_RESET_RETRY_COUNT(lseq), 0);
	asd_write_reg_byte(asd_ha, LmSEQ_NUM_LINK_RESET_RETRIES(lseq), 0);
	asd_write_reg_word(asd_ha, LmSEQ_OOB_INT_ENABLES(lseq), 0);
	/*
	 * Set the desired interval between transmissions of the NOTIFY
	 * (ENABLE SPINUP) primitive.  Must be initialized to val - 1.
	 */
	asd_write_reg_word(asd_ha, LmSEQ_NOTIFY_TIMER_TIMEOUT(lseq),
			   ASD_NOTIFY_TIMEOUT - 1);
	/* No delay for the first NOTIFY to be sent to the attached target. */
	asd_write_reg_word(asd_ha, LmSEQ_NOTIFY_TIMER_DOWN_COUNT(lseq),
			   ASD_NOTIFY_DOWN_COUNT);
	asd_write_reg_word(asd_ha, LmSEQ_NOTIFY_TIMER_INITIAL_COUNT(lseq),
			   ASD_NOTIFY_DOWN_COUNT);

	/* LSEQ Mode dependent, mode 0 and 1, page 1 setup. */
	for (i = 0; i < 2; i++)	{
		int j;
		/* Start from Page 1 of Mode 0 and 1. */
		moffs = LSEQ_PAGE_SIZE + i*LSEQ_MODE_SCRATCH_SIZE;
		/* All the fields of page 1 can be initialized to 0. */
		for (j = 0; j < LSEQ_PAGE_SIZE; j += 4)
			asd_write_reg_dword(asd_ha, LmSCRATCH(lseq)+moffs+j,0);
	}

	/* LSEQ Mode dependent, mode 2, page 1 setup. */
	asd_write_reg_dword(asd_ha, LmSEQ_INVALID_DWORD_COUNT(lseq), 0);
	asd_write_reg_dword(asd_ha, LmSEQ_DISPARITY_ERROR_COUNT(lseq), 0);
	asd_write_reg_dword(asd_ha, LmSEQ_LOSS_OF_SYNC_COUNT(lseq), 0);

	/* LSEQ Mode dependent, mode 4/5, page 1. */
	for (i = 0; i < LSEQ_PAGE_SIZE; i+=4)
		asd_write_reg_dword(asd_ha, LmSEQ_FRAME_TYPE_MASK(lseq)+i, 0);
	asd_write_reg_byte(asd_ha, LmSEQ_FRAME_TYPE_MASK(lseq), 0xFF);
	asd_write_reg_byte(asd_ha, LmSEQ_HASHED_DEST_ADDR_MASK(lseq), 0xFF);
	asd_write_reg_byte(asd_ha, LmSEQ_HASHED_DEST_ADDR_MASK(lseq)+1,0xFF);
	asd_write_reg_byte(asd_ha, LmSEQ_HASHED_DEST_ADDR_MASK(lseq)+2,0xFF);
	asd_write_reg_byte(asd_ha, LmSEQ_HASHED_SRC_ADDR_MASK(lseq), 0xFF);
	asd_write_reg_byte(asd_ha, LmSEQ_HASHED_SRC_ADDR_MASK(lseq)+1, 0xFF);
	asd_write_reg_byte(asd_ha, LmSEQ_HASHED_SRC_ADDR_MASK(lseq)+2, 0xFF);
	asd_write_reg_dword(asd_ha, LmSEQ_DATA_OFFSET(lseq), 0xFFFFFFFF);

	/* LSEQ Mode dependent, mode 0, page 2 setup. */
	asd_write_reg_dword(asd_ha, LmSEQ_SMP_RCV_TIMER_TERM_TS(lseq), 0);
	asd_write_reg_byte(asd_ha, LmSEQ_DEVICE_BITS(lseq), 0);
	asd_write_reg_word(asd_ha, LmSEQ_SDB_DDB(lseq), 0);
	asd_write_reg_byte(asd_ha, LmSEQ_SDB_NUM_TAGS(lseq), 0);
	asd_write_reg_byte(asd_ha, LmSEQ_SDB_CURR_TAG(lseq), 0);

	/* LSEQ Mode Dependent 1, page 2 setup. */
	asd_write_reg_dword(asd_ha, LmSEQ_TX_ID_ADDR_FRAME(lseq), 0);
	asd_write_reg_dword(asd_ha, LmSEQ_TX_ID_ADDR_FRAME(lseq)+4, 0);
	asd_write_reg_dword(asd_ha, LmSEQ_OPEN_TIMER_TERM_TS(lseq), 0);
	asd_write_reg_dword(asd_ha, LmSEQ_SRST_AS_TIMER_TERM_TS(lseq), 0);
	asd_write_reg_dword(asd_ha, LmSEQ_LAST_LOADED_SG_EL(lseq), 0);

	/* LSEQ Mode Dependent 2, page 2 setup. */
	/* The LmSEQ_STP_SHUTDOWN_TIMER_TERM_TS is IGNORED by the sequencer,
	 * i.e. always 0. */
	asd_write_reg_dword(asd_ha, LmSEQ_STP_SHUTDOWN_TIMER_TERM_TS(lseq),0);
	asd_write_reg_dword(asd_ha, LmSEQ_CLOSE_TIMER_TERM_TS(lseq), 0);
	asd_write_reg_dword(asd_ha, LmSEQ_BREAK_TIMER_TERM_TS(lseq), 0);
	asd_write_reg_dword(asd_ha, LmSEQ_DWS_RESET_TIMER_TERM_TS(lseq), 0);
	asd_write_reg_dword(asd_ha,LmSEQ_SATA_INTERLOCK_TIMER_TERM_TS(lseq),0);
	asd_write_reg_dword(asd_ha, LmSEQ_MCTL_TIMER_TERM_TS(lseq), 0);

	/* LSEQ Mode Dependent 4/5, page 2 setup. */
	asd_write_reg_dword(asd_ha, LmSEQ_COMINIT_TIMER_TERM_TS(lseq), 0);
	asd_write_reg_dword(asd_ha, LmSEQ_RCV_ID_TIMER_TERM_TS(lseq), 0);
	asd_write_reg_dword(asd_ha, LmSEQ_RCV_FIS_TIMER_TERM_TS(lseq), 0);
	asd_write_reg_dword(asd_ha, LmSEQ_DEV_PRES_TIMER_TERM_TS(lseq),	0);
}

/**
 * asd_init_lseq_scratch -- setup and init link sequencers
 * @asd_ha: pointer to host adapter struct
 */
static void asd_init_lseq_scratch(struct asd_ha_struct *asd_ha)
{
	u8 lseq;
	u8 lseq_mask;

	lseq_mask = asd_ha->hw_prof.enabled_phys;
	for_each_sequencer(lseq_mask, lseq_mask, lseq) {
		asd_init_lseq_mip(asd_ha, lseq);
		asd_init_lseq_mdp(asd_ha, lseq);
	}
}

/**
 * asd_init_scb_sites -- initialize sequencer SCB sites (memory).
 * @asd_ha: pointer to host adapter structure
 *
 * This should be done before initializing common CSEQ and LSEQ
 * scratch since those areas depend on some computed values here,
 * last_scb_site_no, etc.
 */
static void asd_init_scb_sites(struct asd_ha_struct *asd_ha)
{
	u16	site_no;
	u16     max_scbs = 0;

	for (site_no = asd_ha->hw_prof.max_scbs-1;
	     site_no != (u16) -1;
	     site_no--) {
		u16	i;

		/* Initialize all fields in the SCB site to 0. */
		for (i = 0; i < ASD_SCB_SIZE; i += 4)
			asd_scbsite_write_dword(asd_ha, site_no, i, 0);

		/* Initialize SCB Site Opcode field to invalid. */
		asd_scbsite_write_byte(asd_ha, site_no,
				       offsetof(struct scb_header, opcode),
				       0xFF);

		/* Initialize SCB Site Flags field to mean a response
		 * frame has been received.  This means inadvertent
		 * frames received to be dropped. */
		asd_scbsite_write_byte(asd_ha, site_no, 0x49, 0x01);

		/* Workaround needed by SEQ to fix a SATA issue is to exclude
		 * certain SCB sites from the free list. */
		if (!SCB_SITE_VALID(site_no))
			continue;

		if (last_scb_site_no == 0)
			last_scb_site_no = site_no;

		/* For every SCB site, we need to initialize the
		 * following fields: Q_NEXT, SCB_OPCODE, SCB_FLAGS,
		 * and SG Element Flag. */

		/* Q_NEXT field of the last SCB is invalidated. */
		asd_scbsite_write_word(asd_ha, site_no, 0, first_scb_site_no);

		first_scb_site_no = site_no;
		max_scbs++;
	}
	asd_ha->hw_prof.max_scbs = max_scbs;
	ASD_DPRINTK("max_scbs:%d\n", asd_ha->hw_prof.max_scbs);
	ASD_DPRINTK("first_scb_site_no:0x%x\n", first_scb_site_no);
	ASD_DPRINTK("last_scb_site_no:0x%x\n", last_scb_site_no);
}

/**
 * asd_init_cseq_cio - initialize CSEQ CIO registers
 * @asd_ha: pointer to host adapter structure
 */
static void asd_init_cseq_cio(struct asd_ha_struct *asd_ha)
{
	int i;

	asd_write_reg_byte(asd_ha, CSEQCOMINTEN, 0);
	asd_write_reg_byte(asd_ha, CSEQDLCTL, ASD_DL_SIZE_BITS);
	asd_write_reg_byte(asd_ha, CSEQDLOFFS, 0);
	asd_write_reg_byte(asd_ha, CSEQDLOFFS+1, 0);
	asd_ha->seq.scbpro = 0;
	asd_write_reg_dword(asd_ha, SCBPRO, 0);
	asd_write_reg_dword(asd_ha, CSEQCON, 0);

	/* Initialize CSEQ Mode 11 Interrupt Vectors.
	 * The addresses are 16 bit wide and in dword units.
	 * The values of their macros are in byte units.
	 * Thus we have to divide by 4. */
	asd_write_reg_word(asd_ha, CM11INTVEC0, cseq_vecs[0]);
	asd_write_reg_word(asd_ha, CM11INTVEC1, cseq_vecs[1]);
	asd_write_reg_word(asd_ha, CM11INTVEC2, cseq_vecs[2]);

	/* Enable ARP2HALTC (ARP2 Halted from Halt Code Write). */
	asd_write_reg_byte(asd_ha, CARP2INTEN, EN_ARP2HALTC);

	/* Initialize CSEQ Scratch Page to 0x04. */
	asd_write_reg_byte(asd_ha, CSCRATCHPAGE, 0x04);

	/* Initialize CSEQ Mode[0-8] Dependent registers. */
	/* Initialize Scratch Page to 0. */
	for (i = 0; i < 9; i++)
		asd_write_reg_byte(asd_ha, CMnSCRATCHPAGE(i), 0);

	/* Reset the ARP2 Program Count. */
	asd_write_reg_word(asd_ha, CPRGMCNT, cseq_idle_loop);

	for (i = 0; i < 8; i++) {
		/* Initialize Mode n Link m Interrupt Enable. */
		asd_write_reg_dword(asd_ha, CMnINTEN(i), EN_CMnRSPMBXF);
		/* Initialize Mode n Request Mailbox. */
		asd_write_reg_dword(asd_ha, CMnREQMBX(i), 0);
	}
}

/**
 * asd_init_lseq_cio -- initialize LmSEQ CIO registers
 * @asd_ha: pointer to host adapter structure
 * @lseq:  link sequencer
 */
static void asd_init_lseq_cio(struct asd_ha_struct *asd_ha, int lseq)
{
	u8  *sas_addr;
	int  i;

	/* Enable ARP2HALTC (ARP2 Halted from Halt Code Write). */
	asd_write_reg_dword(asd_ha, LmARP2INTEN(lseq), EN_ARP2HALTC);

	asd_write_reg_byte(asd_ha, LmSCRATCHPAGE(lseq), 0);

	/* Initialize Mode 0,1, and 2 SCRATCHPAGE to 0. */
	for (i = 0; i < 3; i++)
		asd_write_reg_byte(asd_ha, LmMnSCRATCHPAGE(lseq, i), 0);

	/* Initialize Mode 5 SCRATCHPAGE to 0. */
	asd_write_reg_byte(asd_ha, LmMnSCRATCHPAGE(lseq, 5), 0);

	asd_write_reg_dword(asd_ha, LmRSPMBX(lseq), 0);
	/* Initialize Mode 0,1,2 and 5 Interrupt Enable and
	 * Interrupt registers. */
	asd_write_reg_dword(asd_ha, LmMnINTEN(lseq, 0), LmM0INTEN_MASK);
	asd_write_reg_dword(asd_ha, LmMnINT(lseq, 0), 0xFFFFFFFF);
	/* Mode 1 */
	asd_write_reg_dword(asd_ha, LmMnINTEN(lseq, 1), LmM1INTEN_MASK);
	asd_write_reg_dword(asd_ha, LmMnINT(lseq, 1), 0xFFFFFFFF);
	/* Mode 2 */
	asd_write_reg_dword(asd_ha, LmMnINTEN(lseq, 2), LmM2INTEN_MASK);
	asd_write_reg_dword(asd_ha, LmMnINT(lseq, 2), 0xFFFFFFFF);
	/* Mode 5 */
	asd_write_reg_dword(asd_ha, LmMnINTEN(lseq, 5), LmM5INTEN_MASK);
	asd_write_reg_dword(asd_ha, LmMnINT(lseq, 5), 0xFFFFFFFF);

	/* Enable HW Timer status. */
	asd_write_reg_byte(asd_ha, LmHWTSTATEN(lseq), LmHWTSTATEN_MASK);

	/* Enable Primitive Status 0 and 1. */
	asd_write_reg_dword(asd_ha, LmPRIMSTAT0EN(lseq), LmPRIMSTAT0EN_MASK);
	asd_write_reg_dword(asd_ha, LmPRIMSTAT1EN(lseq), LmPRIMSTAT1EN_MASK);

	/* Enable Frame Error. */
	asd_write_reg_dword(asd_ha, LmFRMERREN(lseq), LmFRMERREN_MASK);
	asd_write_reg_byte(asd_ha, LmMnHOLDLVL(lseq, 0), 0x50);

	/* Initialize Mode 0 Transfer Level to 512. */
	asd_write_reg_byte(asd_ha,  LmMnXFRLVL(lseq, 0), LmMnXFRLVL_512);
	/* Initialize Mode 1 Transfer Level to 256. */
	asd_write_reg_byte(asd_ha, LmMnXFRLVL(lseq, 1), LmMnXFRLVL_256);

	/* Initialize Program Count. */
	asd_write_reg_word(asd_ha, LmPRGMCNT(lseq), lseq_idle_loop);

	/* Enable Blind SG Move. */
	asd_write_reg_dword(asd_ha, LmMODECTL(lseq), LmBLIND48);
	asd_write_reg_word(asd_ha, LmM3SATATIMER(lseq),
			   ASD_SATA_INTERLOCK_TIMEOUT);

	(void) asd_read_reg_dword(asd_ha, LmREQMBX(lseq));

	/* Clear Primitive Status 0 and 1. */
	asd_write_reg_dword(asd_ha, LmPRMSTAT0(lseq), 0xFFFFFFFF);
	asd_write_reg_dword(asd_ha, LmPRMSTAT1(lseq), 0xFFFFFFFF);

	/* Clear HW Timer status. */
	asd_write_reg_byte(asd_ha, LmHWTSTAT(lseq), 0xFF);

	/* Clear DMA Errors for Mode 0 and 1. */
	asd_write_reg_byte(asd_ha, LmMnDMAERRS(lseq, 0), 0xFF);
	asd_write_reg_byte(asd_ha, LmMnDMAERRS(lseq, 1), 0xFF);

	/* Clear SG DMA Errors for Mode 0 and 1. */
	asd_write_reg_byte(asd_ha, LmMnSGDMAERRS(lseq, 0), 0xFF);
	asd_write_reg_byte(asd_ha, LmMnSGDMAERRS(lseq, 1), 0xFF);

	/* Clear Mode 0 Buffer Parity Error. */
	asd_write_reg_byte(asd_ha, LmMnBUFSTAT(lseq, 0), LmMnBUFPERR);

	/* Clear Mode 0 Frame Error register. */
	asd_write_reg_dword(asd_ha, LmMnFRMERR(lseq, 0), 0xFFFFFFFF);

	/* Reset LSEQ external interrupt arbiter. */
	asd_write_reg_byte(asd_ha, LmARP2INTCTL(lseq), RSTINTCTL);

	/* Set the Phy SAS for the LmSEQ WWN. */
	sas_addr = asd_ha->phys[lseq].phy_desc->sas_addr;
	for (i = 0; i < SAS_ADDR_SIZE; i++)
		asd_write_reg_byte(asd_ha, LmWWN(lseq) + i, sas_addr[i]);

	/* Set the Transmit Size to 1024 bytes, 0 = 256 Dwords. */
	asd_write_reg_byte(asd_ha, LmMnXMTSIZE(lseq, 1), 0);

	/* Set the Bus Inactivity Time Limit Timer. */
	asd_write_reg_word(asd_ha, LmBITL_TIMER(lseq), 9);

	/* Enable SATA Port Multiplier. */
	asd_write_reg_byte(asd_ha, LmMnSATAFS(lseq, 1), 0x80);

	/* Initialize Interrupt Vector[0-10] address in Mode 3.
	 * See the comment on CSEQ_INT_* */
	asd_write_reg_word(asd_ha, LmM3INTVEC0(lseq), lseq_vecs[0]);
	asd_write_reg_word(asd_ha, LmM3INTVEC1(lseq), lseq_vecs[1]);
	asd_write_reg_word(asd_ha, LmM3INTVEC2(lseq), lseq_vecs[2]);
	asd_write_reg_word(asd_ha, LmM3INTVEC3(lseq), lseq_vecs[3]);
	asd_write_reg_word(asd_ha, LmM3INTVEC4(lseq), lseq_vecs[4]);
	asd_write_reg_word(asd_ha, LmM3INTVEC5(lseq), lseq_vecs[5]);
	asd_write_reg_word(asd_ha, LmM3INTVEC6(lseq), lseq_vecs[6]);
	asd_write_reg_word(asd_ha, LmM3INTVEC7(lseq), lseq_vecs[7]);
	asd_write_reg_word(asd_ha, LmM3INTVEC8(lseq), lseq_vecs[8]);
	asd_write_reg_word(asd_ha, LmM3INTVEC9(lseq), lseq_vecs[9]);
	asd_write_reg_word(asd_ha, LmM3INTVEC10(lseq), lseq_vecs[10]);
	/*
	 * Program the Link LED control, applicable only for
	 * Chip Rev. B or later.
	 */
	asd_write_reg_dword(asd_ha, LmCONTROL(lseq),
			    (LEDTIMER | LEDMODE_TXRX | LEDTIMERS_100ms));

	/* Set the Align Rate for SAS and STP mode. */
	asd_write_reg_byte(asd_ha, LmM1SASALIGN(lseq), SAS_ALIGN_DEFAULT);
	asd_write_reg_byte(asd_ha, LmM1STPALIGN(lseq), STP_ALIGN_DEFAULT);
}


/**
 * asd_post_init_cseq -- clear CSEQ Mode n Int. status and Response mailbox
 * @asd_ha: pointer to host adapter struct
 */
static void asd_post_init_cseq(struct asd_ha_struct *asd_ha)
{
	int i;

	for (i = 0; i < 8; i++)
		asd_write_reg_dword(asd_ha, CMnINT(i), 0xFFFFFFFF);
	for (i = 0; i < 8; i++)
		asd_read_reg_dword(asd_ha, CMnRSPMBX(i));
	/* Reset the external interrupt arbiter. */
	asd_write_reg_byte(asd_ha, CARP2INTCTL, RSTINTCTL);
}

/**
 * asd_init_ddb_0 -- initialize DDB 0
 * @asd_ha: pointer to host adapter structure
 *
 * Initialize DDB site 0 which is used internally by the sequencer.
 */
static void asd_init_ddb_0(struct asd_ha_struct *asd_ha)
{
	int	i;

	/* Zero out the DDB explicitly */
	for (i = 0; i < sizeof(struct asd_ddb_seq_shared); i+=4)
		asd_ddbsite_write_dword(asd_ha, 0, i, 0);

	asd_ddbsite_write_word(asd_ha, 0,
		 offsetof(struct asd_ddb_seq_shared, q_free_ddb_head), 0);
	asd_ddbsite_write_word(asd_ha, 0,
		 offsetof(struct asd_ddb_seq_shared, q_free_ddb_tail),
			       asd_ha->hw_prof.max_ddbs-1);
	asd_ddbsite_write_word(asd_ha, 0,
		 offsetof(struct asd_ddb_seq_shared, q_free_ddb_cnt), 0);
	asd_ddbsite_write_word(asd_ha, 0,
		 offsetof(struct asd_ddb_seq_shared, q_used_ddb_head), 0xFFFF);
	asd_ddbsite_write_word(asd_ha, 0,
		 offsetof(struct asd_ddb_seq_shared, q_used_ddb_tail), 0xFFFF);
	asd_ddbsite_write_word(asd_ha, 0,
		 offsetof(struct asd_ddb_seq_shared, shared_mem_lock), 0);
	asd_ddbsite_write_word(asd_ha, 0,
		 offsetof(struct asd_ddb_seq_shared, smp_conn_tag), 0);
	asd_ddbsite_write_word(asd_ha, 0,
		 offsetof(struct asd_ddb_seq_shared, est_nexus_buf_cnt), 0);
	asd_ddbsite_write_word(asd_ha, 0,
		 offsetof(struct asd_ddb_seq_shared, est_nexus_buf_thresh),
			       asd_ha->hw_prof.num_phys * 2);
	asd_ddbsite_write_byte(asd_ha, 0,
		 offsetof(struct asd_ddb_seq_shared, settable_max_contexts),0);
	asd_ddbsite_write_byte(asd_ha, 0,
	       offsetof(struct asd_ddb_seq_shared, conn_not_active), 0xFF);
	asd_ddbsite_write_byte(asd_ha, 0,
	       offsetof(struct asd_ddb_seq_shared, phy_is_up), 0x00);
	/* DDB 0 is reserved */
	set_bit(0, asd_ha->hw_prof.ddb_bitmap);
}

static void asd_seq_init_ddb_sites(struct asd_ha_struct *asd_ha)
{
	unsigned int i;
	unsigned int ddb_site;

	for (ddb_site = 0 ; ddb_site < ASD_MAX_DDBS; ddb_site++)
		for (i = 0; i < sizeof(struct asd_ddb_ssp_smp_target_port); i+= 4)
			asd_ddbsite_write_dword(asd_ha, ddb_site, i, 0);
}

/**
 * asd_seq_setup_seqs -- setup and initialize central and link sequencers
 * @asd_ha: pointer to host adapter structure
 */
static void asd_seq_setup_seqs(struct asd_ha_struct *asd_ha)
{
	int 		lseq;
	u8		lseq_mask;

	/* Initialize DDB sites */
	asd_seq_init_ddb_sites(asd_ha);

	/* Initialize SCB sites. Done first to compute some values which
	 * the rest of the init code depends on. */
	asd_init_scb_sites(asd_ha);

	/* Initialize CSEQ Scratch RAM registers. */
	asd_init_cseq_scratch(asd_ha);

	/* Initialize LmSEQ Scratch RAM registers. */
	asd_init_lseq_scratch(asd_ha);

	/* Initialize CSEQ CIO registers. */
	asd_init_cseq_cio(asd_ha);

	asd_init_ddb_0(asd_ha);

	/* Initialize LmSEQ CIO registers. */
	lseq_mask = asd_ha->hw_prof.enabled_phys;
	for_each_sequencer(lseq_mask, lseq_mask, lseq)
		asd_init_lseq_cio(asd_ha, lseq);
	asd_post_init_cseq(asd_ha);
}


/**
 * asd_seq_start_cseq -- start the central sequencer, CSEQ
 * @asd_ha: pointer to host adapter structure
 */
static int asd_seq_start_cseq(struct asd_ha_struct *asd_ha)
{
	/* Reset the ARP2 instruction to location zero. */
	asd_write_reg_word(asd_ha, CPRGMCNT, cseq_idle_loop);

	/* Unpause the CSEQ  */
	return asd_unpause_cseq(asd_ha);
}

/**
 * asd_seq_start_lseq -- start a link sequencer
 * @asd_ha: pointer to host adapter structure
 * @lseq: the link sequencer of interest
 */
static int asd_seq_start_lseq(struct asd_ha_struct *asd_ha, int lseq)
{
	/* Reset the ARP2 instruction to location zero. */
	asd_write_reg_word(asd_ha, LmPRGMCNT(lseq), lseq_idle_loop);

	/* Unpause the LmSEQ  */
	return asd_seq_unpause_lseq(asd_ha, lseq);
}

int asd_release_firmware(void)
{
	release_firmware(sequencer_fw);
	return 0;
}

static int asd_request_firmware(struct asd_ha_struct *asd_ha)
{
	int err, i;
	struct sequencer_file_header header;
	const struct sequencer_file_header *hdr_ptr;
	u32 csum = 0;
	u16 *ptr_cseq_vecs, *ptr_lseq_vecs;

	if (sequencer_fw)
		/* already loaded */
		return 0;

	err = request_firmware(&sequencer_fw,
			       SAS_RAZOR_SEQUENCER_FW_FILE,
			       &asd_ha->pcidev->dev);
	if (err)
		return err;

	hdr_ptr = (const struct sequencer_file_header *)sequencer_fw->data;

	header.csum = le32_to_cpu(hdr_ptr->csum);
	header.major = le32_to_cpu(hdr_ptr->major);
	header.minor = le32_to_cpu(hdr_ptr->minor);
	header.cseq_table_offset = le32_to_cpu(hdr_ptr->cseq_table_offset);
	header.cseq_table_size = le32_to_cpu(hdr_ptr->cseq_table_size);
	header.lseq_table_offset = le32_to_cpu(hdr_ptr->lseq_table_offset);
	header.lseq_table_size = le32_to_cpu(hdr_ptr->lseq_table_size);
	header.cseq_code_offset = le32_to_cpu(hdr_ptr->cseq_code_offset);
	header.cseq_code_size = le32_to_cpu(hdr_ptr->cseq_code_size);
	header.lseq_code_offset = le32_to_cpu(hdr_ptr->lseq_code_offset);
	header.lseq_code_size = le32_to_cpu(hdr_ptr->lseq_code_size);
	header.mode2_task = le16_to_cpu(hdr_ptr->mode2_task);
	header.cseq_idle_loop = le16_to_cpu(hdr_ptr->cseq_idle_loop);
	header.lseq_idle_loop = le16_to_cpu(hdr_ptr->lseq_idle_loop);

	for (i = sizeof(header.csum); i < sequencer_fw->size; i++)
		csum += sequencer_fw->data[i];

	if (csum != header.csum) {
		asd_printk("Firmware file checksum mismatch\n");
		return -EINVAL;
	}

	if (header.cseq_table_size != CSEQ_NUM_VECS ||
	    header.lseq_table_size != LSEQ_NUM_VECS) {
		asd_printk("Firmware file table size mismatch\n");
		return -EINVAL;
	}

	asd_printk("Found sequencer Firmware version %d.%d (%s)\n",
		   header.major, header.minor, hdr_ptr->version);

	if (header.major != SAS_RAZOR_SEQUENCER_FW_MAJOR) {
		asd_printk("Firmware Major Version Mismatch;"
			   "driver requires version %d.X",
			   SAS_RAZOR_SEQUENCER_FW_MAJOR);
		return -EINVAL;
	}

	ptr_cseq_vecs = (u16 *)&sequencer_fw->data[header.cseq_table_offset];
	ptr_lseq_vecs = (u16 *)&sequencer_fw->data[header.lseq_table_offset];
	mode2_task = header.mode2_task;
	cseq_idle_loop = header.cseq_idle_loop;
	lseq_idle_loop = header.lseq_idle_loop;

	for (i = 0; i < CSEQ_NUM_VECS; i++)
		cseq_vecs[i] = le16_to_cpu(ptr_cseq_vecs[i]);

	for (i = 0; i < LSEQ_NUM_VECS; i++)
		lseq_vecs[i] = le16_to_cpu(ptr_lseq_vecs[i]);

	cseq_code = &sequencer_fw->data[header.cseq_code_offset];
	cseq_code_size = header.cseq_code_size;
	lseq_code = &sequencer_fw->data[header.lseq_code_offset];
	lseq_code_size = header.lseq_code_size;

	return 0;
}

int asd_init_seqs(struct asd_ha_struct *asd_ha)
{
	int err;

	err = asd_request_firmware(asd_ha);

	if (err) {
		asd_printk("Failed to load sequencer firmware file %s, error %d\n",
			   SAS_RAZOR_SEQUENCER_FW_FILE, err);
		return err;
	}

	err = asd_seq_download_seqs(asd_ha);
	if (err) {
		asd_printk("couldn't download sequencers for %s\n",
			   pci_name(asd_ha->pcidev));
		return err;
	}

	asd_seq_setup_seqs(asd_ha);

	return 0;
}

int asd_start_seqs(struct asd_ha_struct *asd_ha)
{
	int err;
	u8  lseq_mask;
	int lseq;

	err = asd_seq_start_cseq(asd_ha);
	if (err) {
		asd_printk("couldn't start CSEQ for %s\n",
			   pci_name(asd_ha->pcidev));
		return err;
	}

	lseq_mask = asd_ha->hw_prof.enabled_phys;
	for_each_sequencer(lseq_mask, lseq_mask, lseq) {
		err = asd_seq_start_lseq(asd_ha, lseq);
		if (err) {
			asd_printk("couldn't start LSEQ %d for %s\n", lseq,
				   pci_name(asd_ha->pcidev));
			return err;
		}
	}

	return 0;
}

/**
 * asd_update_port_links -- update port_map_by_links and phy_is_up
 * @asd_ha: pointer to host adapter structure
 * @phy: pointer to the phy which has been added to a port
 *
 * 1) When a link reset has completed and we got BYTES DMAED with a
 * valid frame we call this function for that phy, to indicate that
 * the phy is up, i.e. we update the phy_is_up in DDB 0.  The
 * sequencer checks phy_is_up when pending SCBs are to be sent, and
 * when an open address frame has been received.
 *
 * 2) When we know of ports, we call this function to update the map
 * of phys participaing in that port, i.e. we update the
 * port_map_by_links in DDB 0.  When a HARD_RESET primitive has been
 * received, the sequencer disables all phys in that port.
 * port_map_by_links is also used as the conn_mask byte in the
 * initiator/target port DDB.
 */
void asd_update_port_links(struct asd_ha_struct *asd_ha, struct asd_phy *phy)
{
	const u8 phy_mask = (u8) phy->asd_port->phy_mask;
	u8  phy_is_up;
	u8  mask;
	int i, err;
	unsigned long flags;

	spin_lock_irqsave(&asd_ha->hw_prof.ddb_lock, flags);
	for_each_phy(phy_mask, mask, i)
		asd_ddbsite_write_byte(asd_ha, 0,
				       offsetof(struct asd_ddb_seq_shared,
						port_map_by_links)+i,phy_mask);

	for (i = 0; i < 12; i++) {
		phy_is_up = asd_ddbsite_read_byte(asd_ha, 0,
			  offsetof(struct asd_ddb_seq_shared, phy_is_up));
		err = asd_ddbsite_update_byte(asd_ha, 0,
				offsetof(struct asd_ddb_seq_shared, phy_is_up),
				phy_is_up,
				phy_is_up | phy_mask);
		if (!err)
			break;
		else if (err == -EFAULT) {
			asd_printk("phy_is_up: parity error in DDB 0\n");
			break;
		}
	}
	spin_unlock_irqrestore(&asd_ha->hw_prof.ddb_lock, flags);

	if (err)
		asd_printk("couldn't update DDB 0:error:%d\n", err);
}

MODULE_FIRMWARE(SAS_RAZOR_SEQUENCER_FW_FILE);
