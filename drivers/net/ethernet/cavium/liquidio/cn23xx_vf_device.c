/**********************************************************************
 * Author: Cavium, Inc.
 *
 * Contact: support@cavium.com
 *          Please include "LiquidIO" in the subject.
 *
 * Copyright (c) 2003-2016 Cavium, Inc.
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 *
 * This file is distributed in the hope that it will be useful, but
 * AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
 * NONINFRINGEMENT.  See the GNU General Public License for more details.
 ***********************************************************************/
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/vmalloc.h>
#include "liquidio_common.h"
#include "octeon_droq.h"
#include "octeon_iq.h"
#include "response_manager.h"
#include "octeon_device.h"
#include "cn23xx_vf_device.h"
#include "octeon_main.h"
#include "octeon_mailbox.h"

u32 cn23xx_vf_get_oq_ticks(struct octeon_device *oct, u32 time_intr_in_us)
{
	/* This gives the SLI clock per microsec */
	u32 oqticks_per_us = (u32)oct->pfvf_hsword.coproc_tics_per_us;

	/* This gives the clock cycles per millisecond */
	oqticks_per_us *= 1000;

	/* This gives the oq ticks (1024 core clock cycles) per millisecond */
	oqticks_per_us /= 1024;

	/* time_intr is in microseconds. The next 2 steps gives the oq ticks
	 * corressponding to time_intr.
	 */
	oqticks_per_us *= time_intr_in_us;
	oqticks_per_us /= 1000;

	return oqticks_per_us;
}

static int cn23xx_vf_reset_io_queues(struct octeon_device *oct, u32 num_queues)
{
	u32 loop = BUSY_READING_REG_VF_LOOP_COUNT;
	int ret_val = 0;
	u32 q_no;
	u64 d64;

	for (q_no = 0; q_no < num_queues; q_no++) {
		/* set RST bit to 1. This bit applies to both IQ and OQ */
		d64 = octeon_read_csr64(oct,
					CN23XX_VF_SLI_IQ_PKT_CONTROL64(q_no));
		d64 |= CN23XX_PKT_INPUT_CTL_RST;
		octeon_write_csr64(oct, CN23XX_VF_SLI_IQ_PKT_CONTROL64(q_no),
				   d64);
	}

	/* wait until the RST bit is clear or the RST and QUIET bits are set */
	for (q_no = 0; q_no < num_queues; q_no++) {
		u64 reg_val = octeon_read_csr64(oct,
					CN23XX_VF_SLI_IQ_PKT_CONTROL64(q_no));
		while ((READ_ONCE(reg_val) & CN23XX_PKT_INPUT_CTL_RST) &&
		       !(READ_ONCE(reg_val) & CN23XX_PKT_INPUT_CTL_QUIET) &&
		       loop) {
			WRITE_ONCE(reg_val, octeon_read_csr64(
			    oct, CN23XX_VF_SLI_IQ_PKT_CONTROL64(q_no)));
			loop--;
		}
		if (!loop) {
			dev_err(&oct->pci_dev->dev,
				"clearing the reset reg failed or setting the quiet reg failed for qno: %u\n",
				q_no);
			return -1;
		}
		WRITE_ONCE(reg_val, READ_ONCE(reg_val) &
			   ~CN23XX_PKT_INPUT_CTL_RST);
		octeon_write_csr64(oct, CN23XX_VF_SLI_IQ_PKT_CONTROL64(q_no),
				   READ_ONCE(reg_val));

		WRITE_ONCE(reg_val, octeon_read_csr64(
		    oct, CN23XX_VF_SLI_IQ_PKT_CONTROL64(q_no)));
		if (READ_ONCE(reg_val) & CN23XX_PKT_INPUT_CTL_RST) {
			dev_err(&oct->pci_dev->dev,
				"clearing the reset failed for qno: %u\n",
				q_no);
			ret_val = -1;
		}
	}

	return ret_val;
}

static int cn23xx_vf_setup_global_input_regs(struct octeon_device *oct)
{
	struct octeon_cn23xx_vf *cn23xx = (struct octeon_cn23xx_vf *)oct->chip;
	struct octeon_instr_queue *iq;
	u64 q_no, intr_threshold;
	u64 d64;

	if (cn23xx_vf_reset_io_queues(oct, oct->sriov_info.rings_per_vf))
		return -1;

	for (q_no = 0; q_no < (oct->sriov_info.rings_per_vf); q_no++) {
		void __iomem *inst_cnt_reg;

		octeon_write_csr64(oct, CN23XX_VF_SLI_IQ_DOORBELL(q_no),
				   0xFFFFFFFF);
		iq = oct->instr_queue[q_no];

		if (iq)
			inst_cnt_reg = iq->inst_cnt_reg;
		else
			inst_cnt_reg = (u8 *)oct->mmio[0].hw_addr +
				       CN23XX_VF_SLI_IQ_INSTR_COUNT64(q_no);

		d64 = octeon_read_csr64(oct,
					CN23XX_VF_SLI_IQ_INSTR_COUNT64(q_no));

		d64 &= 0xEFFFFFFFFFFFFFFFL;

		octeon_write_csr64(oct, CN23XX_VF_SLI_IQ_INSTR_COUNT64(q_no),
				   d64);

		/* Select ES, RO, NS, RDSIZE,DPTR Fomat#0 for
		 * the Input Queues
		 */
		octeon_write_csr64(oct, CN23XX_VF_SLI_IQ_PKT_CONTROL64(q_no),
				   CN23XX_PKT_INPUT_CTL_MASK);

		/* set the wmark level to trigger PI_INT */
		intr_threshold = CFG_GET_IQ_INTR_PKT(cn23xx->conf) &
				 CN23XX_PKT_IN_DONE_WMARK_MASK;

		writeq((readq(inst_cnt_reg) &
			~(CN23XX_PKT_IN_DONE_WMARK_MASK <<
			  CN23XX_PKT_IN_DONE_WMARK_BIT_POS)) |
		       (intr_threshold << CN23XX_PKT_IN_DONE_WMARK_BIT_POS),
		       inst_cnt_reg);
	}
	return 0;
}

static void cn23xx_vf_setup_global_output_regs(struct octeon_device *oct)
{
	u32 reg_val;
	u32 q_no;

	for (q_no = 0; q_no < (oct->sriov_info.rings_per_vf); q_no++) {
		octeon_write_csr(oct, CN23XX_VF_SLI_OQ_PKTS_CREDIT(q_no),
				 0xFFFFFFFF);

		reg_val =
		    octeon_read_csr(oct, CN23XX_VF_SLI_OQ_PKTS_SENT(q_no));

		reg_val &= 0xEFFFFFFFFFFFFFFFL;

		reg_val =
		    octeon_read_csr(oct, CN23XX_VF_SLI_OQ_PKT_CONTROL(q_no));

		/* clear IPTR */
		reg_val &= ~CN23XX_PKT_OUTPUT_CTL_IPTR;

		/* set DPTR */
		reg_val |= CN23XX_PKT_OUTPUT_CTL_DPTR;

		/* reset BMODE */
		reg_val &= ~(CN23XX_PKT_OUTPUT_CTL_BMODE);

		/* No Relaxed Ordering, No Snoop, 64-bit Byte swap
		 * for Output Queue ScatterList reset ROR_P, NSR_P
		 */
		reg_val &= ~(CN23XX_PKT_OUTPUT_CTL_ROR_P);
		reg_val &= ~(CN23XX_PKT_OUTPUT_CTL_NSR_P);

#ifdef __LITTLE_ENDIAN_BITFIELD
		reg_val &= ~(CN23XX_PKT_OUTPUT_CTL_ES_P);
#else
		reg_val |= (CN23XX_PKT_OUTPUT_CTL_ES_P);
#endif
		/* No Relaxed Ordering, No Snoop, 64-bit Byte swap
		 * for Output Queue Data reset ROR, NSR
		 */
		reg_val &= ~(CN23XX_PKT_OUTPUT_CTL_ROR);
		reg_val &= ~(CN23XX_PKT_OUTPUT_CTL_NSR);
		/* set the ES bit */
		reg_val |= (CN23XX_PKT_OUTPUT_CTL_ES);

		/* write all the selected settings */
		octeon_write_csr(oct, CN23XX_VF_SLI_OQ_PKT_CONTROL(q_no),
				 reg_val);
	}
}

static int cn23xx_setup_vf_device_regs(struct octeon_device *oct)
{
	if (cn23xx_vf_setup_global_input_regs(oct))
		return -1;

	cn23xx_vf_setup_global_output_regs(oct);

	return 0;
}

static void cn23xx_setup_vf_iq_regs(struct octeon_device *oct, u32 iq_no)
{
	struct octeon_instr_queue *iq = oct->instr_queue[iq_no];
	u64 pkt_in_done;

	/* Write the start of the input queue's ring and its size */
	octeon_write_csr64(oct, CN23XX_VF_SLI_IQ_BASE_ADDR64(iq_no),
			   iq->base_addr_dma);
	octeon_write_csr(oct, CN23XX_VF_SLI_IQ_SIZE(iq_no), iq->max_count);

	/* Remember the doorbell & instruction count register addr
	 * for this queue
	 */
	iq->doorbell_reg =
	    (u8 *)oct->mmio[0].hw_addr + CN23XX_VF_SLI_IQ_DOORBELL(iq_no);
	iq->inst_cnt_reg =
	    (u8 *)oct->mmio[0].hw_addr + CN23XX_VF_SLI_IQ_INSTR_COUNT64(iq_no);
	dev_dbg(&oct->pci_dev->dev, "InstQ[%d]:dbell reg @ 0x%p instcnt_reg @ 0x%p\n",
		iq_no, iq->doorbell_reg, iq->inst_cnt_reg);

	/* Store the current instruction counter (used in flush_iq
	 * calculation)
	 */
	pkt_in_done = readq(iq->inst_cnt_reg);

	if (oct->msix_on) {
		/* Set CINT_ENB to enable IQ interrupt */
		writeq((pkt_in_done | CN23XX_INTR_CINT_ENB),
		       iq->inst_cnt_reg);
	}
	iq->reset_instr_cnt = 0;
}

static void cn23xx_setup_vf_oq_regs(struct octeon_device *oct, u32 oq_no)
{
	struct octeon_droq *droq = oct->droq[oq_no];

	octeon_write_csr64(oct, CN23XX_VF_SLI_OQ_BASE_ADDR64(oq_no),
			   droq->desc_ring_dma);
	octeon_write_csr(oct, CN23XX_VF_SLI_OQ_SIZE(oq_no), droq->max_count);

	octeon_write_csr(oct, CN23XX_VF_SLI_OQ_BUFF_INFO_SIZE(oq_no),
			 droq->buffer_size);

	/* Get the mapped address of the pkt_sent and pkts_credit regs */
	droq->pkts_sent_reg =
	    (u8 *)oct->mmio[0].hw_addr + CN23XX_VF_SLI_OQ_PKTS_SENT(oq_no);
	droq->pkts_credit_reg =
	    (u8 *)oct->mmio[0].hw_addr + CN23XX_VF_SLI_OQ_PKTS_CREDIT(oq_no);
}

static void cn23xx_vf_mbox_thread(struct work_struct *work)
{
	struct cavium_wk *wk = (struct cavium_wk *)work;
	struct octeon_mbox *mbox = (struct octeon_mbox *)wk->ctxptr;

	octeon_mbox_process_message(mbox);
}

static int cn23xx_free_vf_mbox(struct octeon_device *oct)
{
	cancel_delayed_work_sync(&oct->mbox[0]->mbox_poll_wk.work);
	vfree(oct->mbox[0]);
	return 0;
}

static int cn23xx_setup_vf_mbox(struct octeon_device *oct)
{
	struct octeon_mbox *mbox = NULL;

	mbox = vmalloc(sizeof(*mbox));
	if (!mbox)
		return 1;

	memset(mbox, 0, sizeof(struct octeon_mbox));

	spin_lock_init(&mbox->lock);

	mbox->oct_dev = oct;

	mbox->q_no = 0;

	mbox->state = OCTEON_MBOX_STATE_IDLE;

	/* VF mbox interrupt reg */
	mbox->mbox_int_reg =
	    (u8 *)oct->mmio[0].hw_addr + CN23XX_VF_SLI_PKT_MBOX_INT(0);
	/* VF reads from SIG0 reg */
	mbox->mbox_read_reg =
	    (u8 *)oct->mmio[0].hw_addr + CN23XX_SLI_PKT_PF_VF_MBOX_SIG(0, 0);
	/* VF writes into SIG1 reg */
	mbox->mbox_write_reg =
	    (u8 *)oct->mmio[0].hw_addr + CN23XX_SLI_PKT_PF_VF_MBOX_SIG(0, 1);

	INIT_DELAYED_WORK(&mbox->mbox_poll_wk.work,
			  cn23xx_vf_mbox_thread);

	mbox->mbox_poll_wk.ctxptr = mbox;

	oct->mbox[0] = mbox;

	writeq(OCTEON_PFVFSIG, mbox->mbox_read_reg);

	return 0;
}

static int cn23xx_enable_vf_io_queues(struct octeon_device *oct)
{
	u32 q_no;

	for (q_no = 0; q_no < oct->num_iqs; q_no++) {
		u64 reg_val;

		/* set the corresponding IQ IS_64B bit */
		if (oct->io_qmask.iq64B & BIT_ULL(q_no)) {
			reg_val = octeon_read_csr64(
			    oct, CN23XX_VF_SLI_IQ_PKT_CONTROL64(q_no));
			reg_val |= CN23XX_PKT_INPUT_CTL_IS_64B;
			octeon_write_csr64(
			    oct, CN23XX_VF_SLI_IQ_PKT_CONTROL64(q_no), reg_val);
		}

		/* set the corresponding IQ ENB bit */
		if (oct->io_qmask.iq & BIT_ULL(q_no)) {
			reg_val = octeon_read_csr64(
			    oct, CN23XX_VF_SLI_IQ_PKT_CONTROL64(q_no));
			reg_val |= CN23XX_PKT_INPUT_CTL_RING_ENB;
			octeon_write_csr64(
			    oct, CN23XX_VF_SLI_IQ_PKT_CONTROL64(q_no), reg_val);
		}
	}
	for (q_no = 0; q_no < oct->num_oqs; q_no++) {
		u32 reg_val;

		/* set the corresponding OQ ENB bit */
		if (oct->io_qmask.oq & BIT_ULL(q_no)) {
			reg_val = octeon_read_csr(
			    oct, CN23XX_VF_SLI_OQ_PKT_CONTROL(q_no));
			reg_val |= CN23XX_PKT_OUTPUT_CTL_RING_ENB;
			octeon_write_csr(
			    oct, CN23XX_VF_SLI_OQ_PKT_CONTROL(q_no), reg_val);
		}
	}

	return 0;
}

static void cn23xx_disable_vf_io_queues(struct octeon_device *oct)
{
	u32 num_queues = oct->num_iqs;

	/* per HRM, rings can only be disabled via reset operation,
	 * NOT via SLI_PKT()_INPUT/OUTPUT_CONTROL[ENB]
	 */
	if (num_queues < oct->num_oqs)
		num_queues = oct->num_oqs;

	cn23xx_vf_reset_io_queues(oct, num_queues);
}

void cn23xx_vf_ask_pf_to_do_flr(struct octeon_device *oct)
{
	struct octeon_mbox_cmd mbox_cmd;

	mbox_cmd.msg.u64 = 0;
	mbox_cmd.msg.s.type = OCTEON_MBOX_REQUEST;
	mbox_cmd.msg.s.resp_needed = 0;
	mbox_cmd.msg.s.cmd = OCTEON_VF_FLR_REQUEST;
	mbox_cmd.msg.s.len = 1;
	mbox_cmd.q_no = 0;
	mbox_cmd.recv_len = 0;
	mbox_cmd.recv_status = 0;
	mbox_cmd.fn = NULL;
	mbox_cmd.fn_arg = 0;

	octeon_mbox_write(oct, &mbox_cmd);
}

static void octeon_pfvf_hs_callback(struct octeon_device *oct,
				    struct octeon_mbox_cmd *cmd,
				    void *arg)
{
	u32 major = 0;

	memcpy((uint8_t *)&oct->pfvf_hsword, cmd->msg.s.params,
	       CN23XX_MAILBOX_MSGPARAM_SIZE);
	if (cmd->recv_len > 1)  {
		major = ((struct lio_version *)(cmd->data))->major;
		major = major << 16;
	}

	atomic_set((atomic_t *)arg, major | 1);
}

int cn23xx_octeon_pfvf_handshake(struct octeon_device *oct)
{
	struct octeon_mbox_cmd mbox_cmd;
	u32 q_no, count = 0;
	atomic_t status;
	u32 pfmajor;
	u32 vfmajor;
	u32 ret;

	/* Sending VF_ACTIVE indication to the PF driver */
	dev_dbg(&oct->pci_dev->dev, "requesting info from pf\n");

	mbox_cmd.msg.u64 = 0;
	mbox_cmd.msg.s.type = OCTEON_MBOX_REQUEST;
	mbox_cmd.msg.s.resp_needed = 1;
	mbox_cmd.msg.s.cmd = OCTEON_VF_ACTIVE;
	mbox_cmd.msg.s.len = 2;
	mbox_cmd.data[0] = 0;
	((struct lio_version *)&mbox_cmd.data[0])->major =
						LIQUIDIO_BASE_MAJOR_VERSION;
	((struct lio_version *)&mbox_cmd.data[0])->minor =
						LIQUIDIO_BASE_MINOR_VERSION;
	((struct lio_version *)&mbox_cmd.data[0])->micro =
						LIQUIDIO_BASE_MICRO_VERSION;
	mbox_cmd.q_no = 0;
	mbox_cmd.recv_len = 0;
	mbox_cmd.recv_status = 0;
	mbox_cmd.fn = (octeon_mbox_callback_t)octeon_pfvf_hs_callback;
	mbox_cmd.fn_arg = &status;

	octeon_mbox_write(oct, &mbox_cmd);

	atomic_set(&status, 0);

	do {
		schedule_timeout_uninterruptible(1);
	} while ((!atomic_read(&status)) && (count++ < 100000));

	ret = atomic_read(&status);
	if (!ret) {
		dev_err(&oct->pci_dev->dev, "octeon_pfvf_handshake timeout\n");
		return 1;
	}

	for (q_no = 0 ; q_no < oct->num_iqs ; q_no++)
		oct->instr_queue[q_no]->txpciq.s.pkind = oct->pfvf_hsword.pkind;

	vfmajor = LIQUIDIO_BASE_MAJOR_VERSION;
	pfmajor = ret >> 16;
	if (pfmajor != vfmajor) {
		dev_err(&oct->pci_dev->dev,
			"VF Liquidio driver (major version %d) is not compatible with Liquidio PF driver (major version %d)\n",
			vfmajor, pfmajor);
		return 1;
	}

	dev_dbg(&oct->pci_dev->dev,
		"VF Liquidio driver (major version %d), Liquidio PF driver (major version %d)\n",
		vfmajor, pfmajor);

	dev_dbg(&oct->pci_dev->dev, "got data from pf pkind is %d\n",
		oct->pfvf_hsword.pkind);

	return 0;
}

static void cn23xx_handle_vf_mbox_intr(struct octeon_ioq_vector *ioq_vector)
{
	struct octeon_device *oct = ioq_vector->oct_dev;
	u64 mbox_int_val;

	if (!ioq_vector->droq_index) {
		/* read and clear by writing 1 */
		mbox_int_val = readq(oct->mbox[0]->mbox_int_reg);
		writeq(mbox_int_val, oct->mbox[0]->mbox_int_reg);
		if (octeon_mbox_read(oct->mbox[0]))
			schedule_delayed_work(&oct->mbox[0]->mbox_poll_wk.work,
					      msecs_to_jiffies(0));
	}
}

static u64 cn23xx_vf_msix_interrupt_handler(void *dev)
{
	struct octeon_ioq_vector *ioq_vector = (struct octeon_ioq_vector *)dev;
	struct octeon_device *oct = ioq_vector->oct_dev;
	struct octeon_droq *droq = oct->droq[ioq_vector->droq_index];
	u64 pkts_sent;
	u64 ret = 0;

	dev_dbg(&oct->pci_dev->dev, "In %s octeon_dev @ %p\n", __func__, oct);
	pkts_sent = readq(droq->pkts_sent_reg);

	/* If our device has interrupted, then proceed. Also check
	 * for all f's if interrupt was triggered on an error
	 * and the PCI read fails.
	 */
	if (!pkts_sent || (pkts_sent == 0xFFFFFFFFFFFFFFFFULL))
		return ret;

	/* Write count reg in sli_pkt_cnts to clear these int. */
	if ((pkts_sent & CN23XX_INTR_PO_INT) ||
	    (pkts_sent & CN23XX_INTR_PI_INT)) {
		if (pkts_sent & CN23XX_INTR_PO_INT)
			ret |= MSIX_PO_INT;
	}

	if (pkts_sent & CN23XX_INTR_PI_INT)
		/* We will clear the count when we update the read_index. */
		ret |= MSIX_PI_INT;

	if (pkts_sent & CN23XX_INTR_MBOX_INT) {
		cn23xx_handle_vf_mbox_intr(ioq_vector);
		ret |= MSIX_MBOX_INT;
	}

	return ret;
}

static u32 cn23xx_update_read_index(struct octeon_instr_queue *iq)
{
	u32 pkt_in_done = readl(iq->inst_cnt_reg);
	u32 last_done;
	u32 new_idx;

	last_done = pkt_in_done - iq->pkt_in_done;
	iq->pkt_in_done = pkt_in_done;

	/* Modulo of the new index with the IQ size will give us
	 * the new index.  The iq->reset_instr_cnt is always zero for
	 * cn23xx, so no extra adjustments are needed.
	 */
	new_idx = (iq->octeon_read_index +
		   (u32)(last_done & CN23XX_PKT_IN_DONE_CNT_MASK)) %
		  iq->max_count;

	return new_idx;
}

static void cn23xx_enable_vf_interrupt(struct octeon_device *oct, u8 intr_flag)
{
	struct octeon_cn23xx_vf *cn23xx = (struct octeon_cn23xx_vf *)oct->chip;
	u32 q_no, time_threshold;

	if (intr_flag & OCTEON_OUTPUT_INTR) {
		for (q_no = 0; q_no < oct->num_oqs; q_no++) {
			/* Set up interrupt packet and time thresholds
			 * for all the OQs
			 */
			time_threshold = cn23xx_vf_get_oq_ticks(
				oct, (u32)CFG_GET_OQ_INTR_TIME(cn23xx->conf));

			octeon_write_csr64(
			    oct, CN23XX_VF_SLI_OQ_PKT_INT_LEVELS(q_no),
			    (CFG_GET_OQ_INTR_PKT(cn23xx->conf) |
			     ((u64)time_threshold << 32)));
		}
	}

	if (intr_flag & OCTEON_INPUT_INTR) {
		for (q_no = 0; q_no < oct->num_oqs; q_no++) {
			/* Set CINT_ENB to enable IQ interrupt */
			octeon_write_csr64(
			    oct, CN23XX_VF_SLI_IQ_INSTR_COUNT64(q_no),
			    ((octeon_read_csr64(
				  oct, CN23XX_VF_SLI_IQ_INSTR_COUNT64(q_no)) &
			      ~CN23XX_PKT_IN_DONE_CNT_MASK) |
			     CN23XX_INTR_CINT_ENB));
		}
	}

	/* Set queue-0 MBOX_ENB to enable VF mailbox interrupt */
	if (intr_flag & OCTEON_MBOX_INTR) {
		octeon_write_csr64(
		    oct, CN23XX_VF_SLI_PKT_MBOX_INT(0),
		    (octeon_read_csr64(oct, CN23XX_VF_SLI_PKT_MBOX_INT(0)) |
		     CN23XX_INTR_MBOX_ENB));
	}
}

static void cn23xx_disable_vf_interrupt(struct octeon_device *oct, u8 intr_flag)
{
	u32 q_no;

	if (intr_flag & OCTEON_OUTPUT_INTR) {
		for (q_no = 0; q_no < oct->num_oqs; q_no++) {
			/* Write all 1's in INT_LEVEL reg to disable PO_INT */
			octeon_write_csr64(
			    oct, CN23XX_VF_SLI_OQ_PKT_INT_LEVELS(q_no),
			    0x3fffffffffffff);
		}
	}
	if (intr_flag & OCTEON_INPUT_INTR) {
		for (q_no = 0; q_no < oct->num_oqs; q_no++) {
			octeon_write_csr64(
			    oct, CN23XX_VF_SLI_IQ_INSTR_COUNT64(q_no),
			    (octeon_read_csr64(
				 oct, CN23XX_VF_SLI_IQ_INSTR_COUNT64(q_no)) &
			     ~(CN23XX_INTR_CINT_ENB |
			       CN23XX_PKT_IN_DONE_CNT_MASK)));
		}
	}

	if (intr_flag & OCTEON_MBOX_INTR) {
		octeon_write_csr64(
		    oct, CN23XX_VF_SLI_PKT_MBOX_INT(0),
		    (octeon_read_csr64(oct, CN23XX_VF_SLI_PKT_MBOX_INT(0)) &
		     ~CN23XX_INTR_MBOX_ENB));
	}
}

int cn23xx_setup_octeon_vf_device(struct octeon_device *oct)
{
	struct octeon_cn23xx_vf *cn23xx = (struct octeon_cn23xx_vf *)oct->chip;
	u32 rings_per_vf, ring_flag;
	u64 reg_val;

	if (octeon_map_pci_barx(oct, 0, 0))
		return 1;

	/* INPUT_CONTROL[RPVF] gives the VF IOq count */
	reg_val = octeon_read_csr64(oct, CN23XX_VF_SLI_IQ_PKT_CONTROL64(0));

	oct->pf_num = (reg_val >> CN23XX_PKT_INPUT_CTL_PF_NUM_POS) &
		      CN23XX_PKT_INPUT_CTL_PF_NUM_MASK;
	oct->vf_num = (reg_val >> CN23XX_PKT_INPUT_CTL_VF_NUM_POS) &
		      CN23XX_PKT_INPUT_CTL_VF_NUM_MASK;

	reg_val = reg_val >> CN23XX_PKT_INPUT_CTL_RPVF_POS;

	rings_per_vf = reg_val & CN23XX_PKT_INPUT_CTL_RPVF_MASK;

	ring_flag = 0;

	cn23xx->conf  = oct_get_config_info(oct, LIO_23XX);
	if (!cn23xx->conf) {
		dev_err(&oct->pci_dev->dev, "%s No Config found for CN23XX\n",
			__func__);
		octeon_unmap_pci_barx(oct, 0);
		return 1;
	}

	if (oct->sriov_info.rings_per_vf > rings_per_vf) {
		dev_warn(&oct->pci_dev->dev,
			 "num_queues:%d greater than PF configured rings_per_vf:%d. Reducing to %d.\n",
			 oct->sriov_info.rings_per_vf, rings_per_vf,
			 rings_per_vf);
		oct->sriov_info.rings_per_vf = rings_per_vf;
	} else {
		if (rings_per_vf > num_present_cpus()) {
			dev_warn(&oct->pci_dev->dev,
				 "PF configured rings_per_vf:%d greater than num_cpu:%d. Using rings_per_vf:%d equal to num cpus\n",
				 rings_per_vf,
				 num_present_cpus(),
				 num_present_cpus());
			oct->sriov_info.rings_per_vf =
				num_present_cpus();
		} else {
			oct->sriov_info.rings_per_vf = rings_per_vf;
		}
	}

	oct->fn_list.setup_iq_regs = cn23xx_setup_vf_iq_regs;
	oct->fn_list.setup_oq_regs = cn23xx_setup_vf_oq_regs;
	oct->fn_list.setup_mbox = cn23xx_setup_vf_mbox;
	oct->fn_list.free_mbox = cn23xx_free_vf_mbox;

	oct->fn_list.msix_interrupt_handler = cn23xx_vf_msix_interrupt_handler;

	oct->fn_list.setup_device_regs = cn23xx_setup_vf_device_regs;
	oct->fn_list.update_iq_read_idx = cn23xx_update_read_index;

	oct->fn_list.enable_interrupt = cn23xx_enable_vf_interrupt;
	oct->fn_list.disable_interrupt = cn23xx_disable_vf_interrupt;

	oct->fn_list.enable_io_queues = cn23xx_enable_vf_io_queues;
	oct->fn_list.disable_io_queues = cn23xx_disable_vf_io_queues;

	return 0;
}

void cn23xx_dump_vf_iq_regs(struct octeon_device *oct)
{
	u32 regval, q_no;

	dev_dbg(&oct->pci_dev->dev, "SLI_IQ_DOORBELL_0 [0x%x]: 0x%016llx\n",
		CN23XX_VF_SLI_IQ_DOORBELL(0),
		CVM_CAST64(octeon_read_csr64(
					oct, CN23XX_VF_SLI_IQ_DOORBELL(0))));

	dev_dbg(&oct->pci_dev->dev, "SLI_IQ_BASEADDR_0 [0x%x]: 0x%016llx\n",
		CN23XX_VF_SLI_IQ_BASE_ADDR64(0),
		CVM_CAST64(octeon_read_csr64(
			oct, CN23XX_VF_SLI_IQ_BASE_ADDR64(0))));

	dev_dbg(&oct->pci_dev->dev, "SLI_IQ_FIFO_RSIZE_0 [0x%x]: 0x%016llx\n",
		CN23XX_VF_SLI_IQ_SIZE(0),
		CVM_CAST64(octeon_read_csr64(oct, CN23XX_VF_SLI_IQ_SIZE(0))));

	for (q_no = 0; q_no < oct->sriov_info.rings_per_vf; q_no++) {
		dev_dbg(&oct->pci_dev->dev, "SLI_PKT[%d]_INPUT_CTL [0x%x]: 0x%016llx\n",
			q_no, CN23XX_VF_SLI_IQ_PKT_CONTROL64(q_no),
			CVM_CAST64(octeon_read_csr64(
				oct, CN23XX_VF_SLI_IQ_PKT_CONTROL64(q_no))));
	}

	pci_read_config_dword(oct->pci_dev, CN23XX_CONFIG_PCIE_DEVCTL, &regval);
	dev_dbg(&oct->pci_dev->dev, "Config DevCtl [0x%x]: 0x%08x\n",
		CN23XX_CONFIG_PCIE_DEVCTL, regval);
}
