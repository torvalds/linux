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

		/* set IPTR & DPTR */
		reg_val |=
		    (CN23XX_PKT_OUTPUT_CTL_IPTR | CN23XX_PKT_OUTPUT_CTL_DPTR);

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

	iq->reset_instr_cnt = 0;
}

static void cn23xx_setup_vf_oq_regs(struct octeon_device *oct, u32 oq_no)
{
	struct octeon_droq *droq = oct->droq[oq_no];

	octeon_write_csr64(oct, CN23XX_VF_SLI_OQ_BASE_ADDR64(oq_no),
			   droq->desc_ring_dma);
	octeon_write_csr(oct, CN23XX_VF_SLI_OQ_SIZE(oq_no), droq->max_count);

	octeon_write_csr(oct, CN23XX_VF_SLI_OQ_BUFF_INFO_SIZE(oq_no),
			 (droq->buffer_size | (OCT_RH_SIZE << 16)));

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
	oct->fn_list.setup_device_regs = cn23xx_setup_vf_device_regs;

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
