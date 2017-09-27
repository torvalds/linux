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
#include "liquidio_common.h"
#include "octeon_droq.h"
#include "octeon_iq.h"
#include "response_manager.h"
#include "octeon_device.h"
#include "octeon_main.h"
#include "cn66xx_regs.h"
#include "cn66xx_device.h"

int lio_cn6xxx_soft_reset(struct octeon_device *oct)
{
	octeon_write_csr64(oct, CN6XXX_WIN_WR_MASK_REG, 0xFF);

	dev_dbg(&oct->pci_dev->dev, "BIST enabled for soft reset\n");

	lio_pci_writeq(oct, 1, CN6XXX_CIU_SOFT_BIST);
	octeon_write_csr64(oct, CN6XXX_SLI_SCRATCH1, 0x1234ULL);

	lio_pci_readq(oct, CN6XXX_CIU_SOFT_RST);
	lio_pci_writeq(oct, 1, CN6XXX_CIU_SOFT_RST);

	/* make sure that the reset is written before starting timer */
	mmiowb();

	/* Wait for 10ms as Octeon resets. */
	mdelay(100);

	if (octeon_read_csr64(oct, CN6XXX_SLI_SCRATCH1)) {
		dev_err(&oct->pci_dev->dev, "Soft reset failed\n");
		return 1;
	}

	dev_dbg(&oct->pci_dev->dev, "Reset completed\n");
	octeon_write_csr64(oct, CN6XXX_WIN_WR_MASK_REG, 0xFF);

	return 0;
}

void lio_cn6xxx_enable_error_reporting(struct octeon_device *oct)
{
	u32 val;

	pci_read_config_dword(oct->pci_dev, CN6XXX_PCIE_DEVCTL, &val);
	if (val & 0x000c0000) {
		dev_err(&oct->pci_dev->dev, "PCI-E Link error detected: 0x%08x\n",
			val & 0x000c0000);
	}

	val |= 0xf;          /* Enable Link error reporting */

	dev_dbg(&oct->pci_dev->dev, "Enabling PCI-E error reporting..\n");
	pci_write_config_dword(oct->pci_dev, CN6XXX_PCIE_DEVCTL, val);
}

void lio_cn6xxx_setup_pcie_mps(struct octeon_device *oct,
			       enum octeon_pcie_mps mps)
{
	u32 val;
	u64 r64;

	/* Read config register for MPS */
	pci_read_config_dword(oct->pci_dev, CN6XXX_PCIE_DEVCTL, &val);

	if (mps == PCIE_MPS_DEFAULT) {
		mps = ((val & (0x7 << 5)) >> 5);
	} else {
		val &= ~(0x7 << 5);  /* Turn off any MPS bits */
		val |= (mps << 5);   /* Set MPS */
		pci_write_config_dword(oct->pci_dev, CN6XXX_PCIE_DEVCTL, val);
	}

	/* Set MPS in DPI_SLI_PRT0_CFG to the same value. */
	r64 = lio_pci_readq(oct, CN6XXX_DPI_SLI_PRTX_CFG(oct->pcie_port));
	r64 |= (mps << 4);
	lio_pci_writeq(oct, r64, CN6XXX_DPI_SLI_PRTX_CFG(oct->pcie_port));
}

void lio_cn6xxx_setup_pcie_mrrs(struct octeon_device *oct,
				enum octeon_pcie_mrrs mrrs)
{
	u32 val;
	u64 r64;

	/* Read config register for MRRS */
	pci_read_config_dword(oct->pci_dev, CN6XXX_PCIE_DEVCTL, &val);

	if (mrrs == PCIE_MRRS_DEFAULT) {
		mrrs = ((val & (0x7 << 12)) >> 12);
	} else {
		val &= ~(0x7 << 12); /* Turn off any MRRS bits */
		val |= (mrrs << 12); /* Set MRRS */
		pci_write_config_dword(oct->pci_dev, CN6XXX_PCIE_DEVCTL, val);
	}

	/* Set MRRS in SLI_S2M_PORT0_CTL to the same value. */
	r64 = octeon_read_csr64(oct, CN6XXX_SLI_S2M_PORTX_CTL(oct->pcie_port));
	r64 |= mrrs;
	octeon_write_csr64(oct, CN6XXX_SLI_S2M_PORTX_CTL(oct->pcie_port), r64);

	/* Set MRRS in DPI_SLI_PRT0_CFG to the same value. */
	r64 = lio_pci_readq(oct, CN6XXX_DPI_SLI_PRTX_CFG(oct->pcie_port));
	r64 |= mrrs;
	lio_pci_writeq(oct, r64, CN6XXX_DPI_SLI_PRTX_CFG(oct->pcie_port));
}

u32 lio_cn6xxx_coprocessor_clock(struct octeon_device *oct)
{
	/* Bits 29:24 of MIO_RST_BOOT holds the ref. clock multiplier
	 * for SLI.
	 */
	return ((lio_pci_readq(oct, CN6XXX_MIO_RST_BOOT) >> 24) & 0x3f) * 50;
}

u32 lio_cn6xxx_get_oq_ticks(struct octeon_device *oct,
			    u32 time_intr_in_us)
{
	/* This gives the SLI clock per microsec */
	u32 oqticks_per_us = lio_cn6xxx_coprocessor_clock(oct);

	/* core clock per us / oq ticks will be fractional. TO avoid that
	 * we use the method below.
	 */

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

void lio_cn6xxx_setup_global_input_regs(struct octeon_device *oct)
{
	/* Select Round-Robin Arb, ES, RO, NS for Input Queues */
	octeon_write_csr(oct, CN6XXX_SLI_PKT_INPUT_CONTROL,
			 CN6XXX_INPUT_CTL_MASK);

	/* Instruction Read Size - Max 4 instructions per PCIE Read */
	octeon_write_csr64(oct, CN6XXX_SLI_PKT_INSTR_RD_SIZE,
			   0xFFFFFFFFFFFFFFFFULL);

	/* Select PCIE Port for all Input rings. */
	octeon_write_csr64(oct, CN6XXX_SLI_IN_PCIE_PORT,
			   (oct->pcie_port * 0x5555555555555555ULL));
}

static void lio_cn66xx_setup_pkt_ctl_regs(struct octeon_device *oct)
{
	u64 pktctl;

	struct octeon_cn6xxx *cn6xxx = (struct octeon_cn6xxx *)oct->chip;

	pktctl = octeon_read_csr64(oct, CN6XXX_SLI_PKT_CTL);

	/* 66XX SPECIFIC */
	if (CFG_GET_OQ_MAX_Q(cn6xxx->conf) <= 4)
		/* Disable RING_EN if only upto 4 rings are used. */
		pktctl &= ~(1 << 4);
	else
		pktctl |= (1 << 4);

	if (CFG_GET_IS_SLI_BP_ON(cn6xxx->conf))
		pktctl |= 0xF;
	else
		/* Disable per-port backpressure. */
		pktctl &= ~0xF;
	octeon_write_csr64(oct, CN6XXX_SLI_PKT_CTL, pktctl);
}

void lio_cn6xxx_setup_global_output_regs(struct octeon_device *oct)
{
	u32 time_threshold;
	struct octeon_cn6xxx *cn6xxx = (struct octeon_cn6xxx *)oct->chip;

	/* / Select PCI-E Port for all Output queues */
	octeon_write_csr64(oct, CN6XXX_SLI_PKT_PCIE_PORT64,
			   (oct->pcie_port * 0x5555555555555555ULL));

	if (CFG_GET_IS_SLI_BP_ON(cn6xxx->conf)) {
		octeon_write_csr64(oct, CN6XXX_SLI_OQ_WMARK, 32);
	} else {
		/* / Set Output queue watermark to 0 to disable backpressure */
		octeon_write_csr64(oct, CN6XXX_SLI_OQ_WMARK, 0);
	}

	/* / Select Packet count instead of bytes for SLI_PKTi_CNTS[CNT] */
	octeon_write_csr(oct, CN6XXX_SLI_PKT_OUT_BMODE, 0);

	/* Select ES, RO, NS setting from register for Output Queue Packet
	 * Address
	 */
	octeon_write_csr(oct, CN6XXX_SLI_PKT_DPADDR, 0xFFFFFFFF);

	/* No Relaxed Ordering, No Snoop, 64-bit swap for Output
	 * Queue ScatterList
	 */
	octeon_write_csr(oct, CN6XXX_SLI_PKT_SLIST_ROR, 0);
	octeon_write_csr(oct, CN6XXX_SLI_PKT_SLIST_NS, 0);

	/* / ENDIAN_SPECIFIC CHANGES - 0 works for LE. */
#ifdef __BIG_ENDIAN_BITFIELD
	octeon_write_csr64(oct, CN6XXX_SLI_PKT_SLIST_ES64,
			   0x5555555555555555ULL);
#else
	octeon_write_csr64(oct, CN6XXX_SLI_PKT_SLIST_ES64, 0ULL);
#endif

	/* / No Relaxed Ordering, No Snoop, 64-bit swap for Output Queue Data */
	octeon_write_csr(oct, CN6XXX_SLI_PKT_DATA_OUT_ROR, 0);
	octeon_write_csr(oct, CN6XXX_SLI_PKT_DATA_OUT_NS, 0);
	octeon_write_csr64(oct, CN6XXX_SLI_PKT_DATA_OUT_ES64,
			   0x5555555555555555ULL);

	/* / Set up interrupt packet and time threshold */
	octeon_write_csr(oct, CN6XXX_SLI_OQ_INT_LEVEL_PKTS,
			 (u32)CFG_GET_OQ_INTR_PKT(cn6xxx->conf));
	time_threshold =
		lio_cn6xxx_get_oq_ticks(oct, (u32)
					CFG_GET_OQ_INTR_TIME(cn6xxx->conf));

	octeon_write_csr(oct, CN6XXX_SLI_OQ_INT_LEVEL_TIME, time_threshold);
}

static int lio_cn6xxx_setup_device_regs(struct octeon_device *oct)
{
	lio_cn6xxx_setup_pcie_mps(oct, PCIE_MPS_DEFAULT);
	lio_cn6xxx_setup_pcie_mrrs(oct, PCIE_MRRS_512B);
	lio_cn6xxx_enable_error_reporting(oct);

	lio_cn6xxx_setup_global_input_regs(oct);
	lio_cn66xx_setup_pkt_ctl_regs(oct);
	lio_cn6xxx_setup_global_output_regs(oct);

	/* Default error timeout value should be 0x200000 to avoid host hang
	 * when reads invalid register
	 */
	octeon_write_csr64(oct, CN6XXX_SLI_WINDOW_CTL, 0x200000ULL);
	return 0;
}

void lio_cn6xxx_setup_iq_regs(struct octeon_device *oct, u32 iq_no)
{
	struct octeon_instr_queue *iq = oct->instr_queue[iq_no];

	octeon_write_csr64(oct, CN6XXX_SLI_IQ_PKT_INSTR_HDR64(iq_no), 0);

	/* Write the start of the input queue's ring and its size  */
	octeon_write_csr64(oct, CN6XXX_SLI_IQ_BASE_ADDR64(iq_no),
			   iq->base_addr_dma);
	octeon_write_csr(oct, CN6XXX_SLI_IQ_SIZE(iq_no), iq->max_count);

	/* Remember the doorbell & instruction count register addr for this
	 * queue
	 */
	iq->doorbell_reg = oct->mmio[0].hw_addr + CN6XXX_SLI_IQ_DOORBELL(iq_no);
	iq->inst_cnt_reg = oct->mmio[0].hw_addr
			   + CN6XXX_SLI_IQ_INSTR_COUNT(iq_no);
	dev_dbg(&oct->pci_dev->dev, "InstQ[%d]:dbell reg @ 0x%p instcnt_reg @ 0x%p\n",
		iq_no, iq->doorbell_reg, iq->inst_cnt_reg);

	/* Store the current instruction counter
	 * (used in flush_iq calculation)
	 */
	iq->reset_instr_cnt = readl(iq->inst_cnt_reg);
}

static void lio_cn66xx_setup_iq_regs(struct octeon_device *oct, u32 iq_no)
{
	lio_cn6xxx_setup_iq_regs(oct, iq_no);

	/* Backpressure for this queue - WMARK set to all F's. This effectively
	 * disables the backpressure mechanism.
	 */
	octeon_write_csr64(oct, CN66XX_SLI_IQ_BP64(iq_no),
			   (0xFFFFFFFFULL << 32));
}

void lio_cn6xxx_setup_oq_regs(struct octeon_device *oct, u32 oq_no)
{
	u32 intr;
	struct octeon_droq *droq = oct->droq[oq_no];

	octeon_write_csr64(oct, CN6XXX_SLI_OQ_BASE_ADDR64(oq_no),
			   droq->desc_ring_dma);
	octeon_write_csr(oct, CN6XXX_SLI_OQ_SIZE(oq_no), droq->max_count);

	octeon_write_csr(oct, CN6XXX_SLI_OQ_BUFF_INFO_SIZE(oq_no),
			 droq->buffer_size);

	/* Get the mapped address of the pkt_sent and pkts_credit regs */
	droq->pkts_sent_reg =
		oct->mmio[0].hw_addr + CN6XXX_SLI_OQ_PKTS_SENT(oq_no);
	droq->pkts_credit_reg =
		oct->mmio[0].hw_addr + CN6XXX_SLI_OQ_PKTS_CREDIT(oq_no);

	/* Enable this output queue to generate Packet Timer Interrupt */
	intr = octeon_read_csr(oct, CN6XXX_SLI_PKT_TIME_INT_ENB);
	intr |= (1 << oq_no);
	octeon_write_csr(oct, CN6XXX_SLI_PKT_TIME_INT_ENB, intr);

	/* Enable this output queue to generate Packet Timer Interrupt */
	intr = octeon_read_csr(oct, CN6XXX_SLI_PKT_CNT_INT_ENB);
	intr |= (1 << oq_no);
	octeon_write_csr(oct, CN6XXX_SLI_PKT_CNT_INT_ENB, intr);
}

int lio_cn6xxx_enable_io_queues(struct octeon_device *oct)
{
	u32 mask;

	mask = octeon_read_csr(oct, CN6XXX_SLI_PKT_INSTR_SIZE);
	mask |= oct->io_qmask.iq64B;
	octeon_write_csr(oct, CN6XXX_SLI_PKT_INSTR_SIZE, mask);

	mask = octeon_read_csr(oct, CN6XXX_SLI_PKT_INSTR_ENB);
	mask |= oct->io_qmask.iq;
	octeon_write_csr(oct, CN6XXX_SLI_PKT_INSTR_ENB, mask);

	mask = octeon_read_csr(oct, CN6XXX_SLI_PKT_OUT_ENB);
	mask |= oct->io_qmask.oq;
	octeon_write_csr(oct, CN6XXX_SLI_PKT_OUT_ENB, mask);

	return 0;
}

void lio_cn6xxx_disable_io_queues(struct octeon_device *oct)
{
	int i;
	u32 mask, loop = HZ;
	u32 d32;

	/* Reset the Enable bits for Input Queues. */
	mask = octeon_read_csr(oct, CN6XXX_SLI_PKT_INSTR_ENB);
	mask ^= oct->io_qmask.iq;
	octeon_write_csr(oct, CN6XXX_SLI_PKT_INSTR_ENB, mask);

	/* Wait until hardware indicates that the queues are out of reset. */
	mask = (u32)oct->io_qmask.iq;
	d32 = octeon_read_csr(oct, CN6XXX_SLI_PORT_IN_RST_IQ);
	while (((d32 & mask) != mask) && loop--) {
		d32 = octeon_read_csr(oct, CN6XXX_SLI_PORT_IN_RST_IQ);
		schedule_timeout_uninterruptible(1);
	}

	/* Reset the doorbell register for each Input queue. */
	for (i = 0; i < MAX_OCTEON_INSTR_QUEUES(oct); i++) {
		if (!(oct->io_qmask.iq & BIT_ULL(i)))
			continue;
		octeon_write_csr(oct, CN6XXX_SLI_IQ_DOORBELL(i), 0xFFFFFFFF);
		d32 = octeon_read_csr(oct, CN6XXX_SLI_IQ_DOORBELL(i));
	}

	/* Reset the Enable bits for Output Queues. */
	mask = octeon_read_csr(oct, CN6XXX_SLI_PKT_OUT_ENB);
	mask ^= oct->io_qmask.oq;
	octeon_write_csr(oct, CN6XXX_SLI_PKT_OUT_ENB, mask);

	/* Wait until hardware indicates that the queues are out of reset. */
	loop = HZ;
	mask = (u32)oct->io_qmask.oq;
	d32 = octeon_read_csr(oct, CN6XXX_SLI_PORT_IN_RST_OQ);
	while (((d32 & mask) != mask) && loop--) {
		d32 = octeon_read_csr(oct, CN6XXX_SLI_PORT_IN_RST_OQ);
		schedule_timeout_uninterruptible(1);
	}
	;

	/* Reset the doorbell register for each Output queue. */
	for (i = 0; i < MAX_OCTEON_OUTPUT_QUEUES(oct); i++) {
		if (!(oct->io_qmask.oq & BIT_ULL(i)))
			continue;
		octeon_write_csr(oct, CN6XXX_SLI_OQ_PKTS_CREDIT(i), 0xFFFFFFFF);
		d32 = octeon_read_csr(oct, CN6XXX_SLI_OQ_PKTS_CREDIT(i));

		d32 = octeon_read_csr(oct, CN6XXX_SLI_OQ_PKTS_SENT(i));
		octeon_write_csr(oct, CN6XXX_SLI_OQ_PKTS_SENT(i), d32);
	}

	d32 = octeon_read_csr(oct, CN6XXX_SLI_PKT_CNT_INT);
	if (d32)
		octeon_write_csr(oct, CN6XXX_SLI_PKT_CNT_INT, d32);

	d32 = octeon_read_csr(oct, CN6XXX_SLI_PKT_TIME_INT);
	if (d32)
		octeon_write_csr(oct, CN6XXX_SLI_PKT_TIME_INT, d32);
}

void
lio_cn6xxx_bar1_idx_setup(struct octeon_device *oct,
			  u64 core_addr,
			  u32 idx,
			  int valid)
{
	u64 bar1;

	if (valid == 0) {
		bar1 = lio_pci_readq(oct, CN6XXX_BAR1_REG(idx, oct->pcie_port));
		lio_pci_writeq(oct, (bar1 & 0xFFFFFFFEULL),
			       CN6XXX_BAR1_REG(idx, oct->pcie_port));
		bar1 = lio_pci_readq(oct, CN6XXX_BAR1_REG(idx, oct->pcie_port));
		return;
	}

	/* Bits 17:4 of the PCI_BAR1_INDEXx stores bits 35:22 of
	 * the Core Addr
	 */
	lio_pci_writeq(oct, (((core_addr >> 22) << 4) | PCI_BAR1_MASK),
		       CN6XXX_BAR1_REG(idx, oct->pcie_port));

	bar1 = lio_pci_readq(oct, CN6XXX_BAR1_REG(idx, oct->pcie_port));
}

void lio_cn6xxx_bar1_idx_write(struct octeon_device *oct,
			       u32 idx,
			       u32 mask)
{
	lio_pci_writeq(oct, mask, CN6XXX_BAR1_REG(idx, oct->pcie_port));
}

u32 lio_cn6xxx_bar1_idx_read(struct octeon_device *oct, u32 idx)
{
	return (u32)lio_pci_readq(oct, CN6XXX_BAR1_REG(idx, oct->pcie_port));
}

u32
lio_cn6xxx_update_read_index(struct octeon_instr_queue *iq)
{
	u32 new_idx = readl(iq->inst_cnt_reg);

	/* The new instr cnt reg is a 32-bit counter that can roll over. We have
	 * noted the counter's initial value at init time into
	 * reset_instr_cnt
	 */
	if (iq->reset_instr_cnt < new_idx)
		new_idx -= iq->reset_instr_cnt;
	else
		new_idx += (0xffffffff - iq->reset_instr_cnt) + 1;

	/* Modulo of the new index with the IQ size will give us
	 * the new index.
	 */
	new_idx %= iq->max_count;

	return new_idx;
}

void lio_cn6xxx_enable_interrupt(struct octeon_device *oct,
				 u8 unused __attribute__((unused)))
{
	struct octeon_cn6xxx *cn6xxx = (struct octeon_cn6xxx *)oct->chip;
	u64 mask = cn6xxx->intr_mask64 | CN6XXX_INTR_DMA0_FORCE;

	/* Enable Interrupt */
	writeq(mask, cn6xxx->intr_enb_reg64);
}

void lio_cn6xxx_disable_interrupt(struct octeon_device *oct,
				  u8 unused __attribute__((unused)))
{
	struct octeon_cn6xxx *cn6xxx = (struct octeon_cn6xxx *)oct->chip;

	/* Disable Interrupts */
	writeq(0, cn6xxx->intr_enb_reg64);

	/* make sure interrupts are really disabled */
	mmiowb();
}

static void lio_cn6xxx_get_pcie_qlmport(struct octeon_device *oct)
{
	/* CN63xx Pass2 and newer parts implements the SLI_MAC_NUMBER register
	 * to determine the PCIE port #
	 */
	oct->pcie_port = octeon_read_csr(oct, CN6XXX_SLI_MAC_NUMBER) & 0xff;

	dev_dbg(&oct->pci_dev->dev, "Using PCIE Port %d\n", oct->pcie_port);
}

static void
lio_cn6xxx_process_pcie_error_intr(struct octeon_device *oct, u64 intr64)
{
	dev_err(&oct->pci_dev->dev, "Error Intr: 0x%016llx\n",
		CVM_CAST64(intr64));
}

static int lio_cn6xxx_process_droq_intr_regs(struct octeon_device *oct)
{
	struct octeon_droq *droq;
	int oq_no;
	u32 pkt_count, droq_time_mask, droq_mask, droq_int_enb;
	u32 droq_cnt_enb, droq_cnt_mask;

	droq_cnt_enb = octeon_read_csr(oct, CN6XXX_SLI_PKT_CNT_INT_ENB);
	droq_cnt_mask = octeon_read_csr(oct, CN6XXX_SLI_PKT_CNT_INT);
	droq_mask = droq_cnt_mask & droq_cnt_enb;

	droq_time_mask = octeon_read_csr(oct, CN6XXX_SLI_PKT_TIME_INT);
	droq_int_enb = octeon_read_csr(oct, CN6XXX_SLI_PKT_TIME_INT_ENB);
	droq_mask |= (droq_time_mask & droq_int_enb);

	droq_mask &= oct->io_qmask.oq;

	oct->droq_intr = 0;

	for (oq_no = 0; oq_no < MAX_OCTEON_OUTPUT_QUEUES(oct); oq_no++) {
		if (!(droq_mask & BIT_ULL(oq_no)))
			continue;

		droq = oct->droq[oq_no];
		pkt_count = octeon_droq_check_hw_for_pkts(droq);
		if (pkt_count) {
			oct->droq_intr |= BIT_ULL(oq_no);
			if (droq->ops.poll_mode) {
				u32 value;
				u32 reg;

				struct octeon_cn6xxx *cn6xxx =
					(struct octeon_cn6xxx *)oct->chip;

				/* disable interrupts for this droq */
				spin_lock
					(&cn6xxx->lock_for_droq_int_enb_reg);
				reg = CN6XXX_SLI_PKT_TIME_INT_ENB;
				value = octeon_read_csr(oct, reg);
				value &= ~(1 << oq_no);
				octeon_write_csr(oct, reg, value);
				reg = CN6XXX_SLI_PKT_CNT_INT_ENB;
				value = octeon_read_csr(oct, reg);
				value &= ~(1 << oq_no);
				octeon_write_csr(oct, reg, value);

				/* Ensure that the enable register is written.
				 */
				mmiowb();

				spin_unlock(&cn6xxx->lock_for_droq_int_enb_reg);
			}
		}
	}

	droq_time_mask &= oct->io_qmask.oq;
	droq_cnt_mask &= oct->io_qmask.oq;

	/* Reset the PKT_CNT/TIME_INT registers. */
	if (droq_time_mask)
		octeon_write_csr(oct, CN6XXX_SLI_PKT_TIME_INT, droq_time_mask);

	if (droq_cnt_mask)      /* reset PKT_CNT register:66xx */
		octeon_write_csr(oct, CN6XXX_SLI_PKT_CNT_INT, droq_cnt_mask);

	return 0;
}

irqreturn_t lio_cn6xxx_process_interrupt_regs(void *dev)
{
	struct octeon_device *oct = (struct octeon_device *)dev;
	struct octeon_cn6xxx *cn6xxx = (struct octeon_cn6xxx *)oct->chip;
	u64 intr64;

	intr64 = readq(cn6xxx->intr_sum_reg64);

	/* If our device has interrupted, then proceed.
	 * Also check for all f's if interrupt was triggered on an error
	 * and the PCI read fails.
	 */
	if (!intr64 || (intr64 == 0xFFFFFFFFFFFFFFFFULL))
		return IRQ_NONE;

	oct->int_status = 0;

	if (intr64 & CN6XXX_INTR_ERR)
		lio_cn6xxx_process_pcie_error_intr(oct, intr64);

	if (intr64 & CN6XXX_INTR_PKT_DATA) {
		lio_cn6xxx_process_droq_intr_regs(oct);
		oct->int_status |= OCT_DEV_INTR_PKT_DATA;
	}

	if (intr64 & CN6XXX_INTR_DMA0_FORCE)
		oct->int_status |= OCT_DEV_INTR_DMA0_FORCE;

	if (intr64 & CN6XXX_INTR_DMA1_FORCE)
		oct->int_status |= OCT_DEV_INTR_DMA1_FORCE;

	/* Clear the current interrupts */
	writeq(intr64, cn6xxx->intr_sum_reg64);

	return IRQ_HANDLED;
}

void lio_cn6xxx_setup_reg_address(struct octeon_device *oct,
				  void *chip,
				  struct octeon_reg_list *reg_list)
{
	u8 __iomem *bar0_pciaddr = oct->mmio[0].hw_addr;
	struct octeon_cn6xxx *cn6xxx = (struct octeon_cn6xxx *)chip;

	reg_list->pci_win_wr_addr_hi =
		(u32 __iomem *)(bar0_pciaddr + CN6XXX_WIN_WR_ADDR_HI);
	reg_list->pci_win_wr_addr_lo =
		(u32 __iomem *)(bar0_pciaddr + CN6XXX_WIN_WR_ADDR_LO);
	reg_list->pci_win_wr_addr =
		(u64 __iomem *)(bar0_pciaddr + CN6XXX_WIN_WR_ADDR64);

	reg_list->pci_win_rd_addr_hi =
		(u32 __iomem *)(bar0_pciaddr + CN6XXX_WIN_RD_ADDR_HI);
	reg_list->pci_win_rd_addr_lo =
		(u32 __iomem *)(bar0_pciaddr + CN6XXX_WIN_RD_ADDR_LO);
	reg_list->pci_win_rd_addr =
		(u64 __iomem *)(bar0_pciaddr + CN6XXX_WIN_RD_ADDR64);

	reg_list->pci_win_wr_data_hi =
		(u32 __iomem *)(bar0_pciaddr + CN6XXX_WIN_WR_DATA_HI);
	reg_list->pci_win_wr_data_lo =
		(u32 __iomem *)(bar0_pciaddr + CN6XXX_WIN_WR_DATA_LO);
	reg_list->pci_win_wr_data =
		(u64 __iomem *)(bar0_pciaddr + CN6XXX_WIN_WR_DATA64);

	reg_list->pci_win_rd_data_hi =
		(u32 __iomem *)(bar0_pciaddr + CN6XXX_WIN_RD_DATA_HI);
	reg_list->pci_win_rd_data_lo =
		(u32 __iomem *)(bar0_pciaddr + CN6XXX_WIN_RD_DATA_LO);
	reg_list->pci_win_rd_data =
		(u64 __iomem *)(bar0_pciaddr + CN6XXX_WIN_RD_DATA64);

	lio_cn6xxx_get_pcie_qlmport(oct);

	cn6xxx->intr_sum_reg64 = bar0_pciaddr + CN6XXX_SLI_INT_SUM64;
	cn6xxx->intr_mask64 = CN6XXX_INTR_MASK;
	cn6xxx->intr_enb_reg64 =
		bar0_pciaddr + CN6XXX_SLI_INT_ENB64(oct->pcie_port);
}

int lio_setup_cn66xx_octeon_device(struct octeon_device *oct)
{
	struct octeon_cn6xxx *cn6xxx = (struct octeon_cn6xxx *)oct->chip;

	if (octeon_map_pci_barx(oct, 0, 0))
		return 1;

	if (octeon_map_pci_barx(oct, 1, MAX_BAR1_IOREMAP_SIZE)) {
		dev_err(&oct->pci_dev->dev, "%s CN66XX BAR1 map failed\n",
			__func__);
		octeon_unmap_pci_barx(oct, 0);
		return 1;
	}

	spin_lock_init(&cn6xxx->lock_for_droq_int_enb_reg);

	oct->fn_list.setup_iq_regs = lio_cn66xx_setup_iq_regs;
	oct->fn_list.setup_oq_regs = lio_cn6xxx_setup_oq_regs;

	oct->fn_list.soft_reset = lio_cn6xxx_soft_reset;
	oct->fn_list.setup_device_regs = lio_cn6xxx_setup_device_regs;
	oct->fn_list.update_iq_read_idx = lio_cn6xxx_update_read_index;

	oct->fn_list.bar1_idx_setup = lio_cn6xxx_bar1_idx_setup;
	oct->fn_list.bar1_idx_write = lio_cn6xxx_bar1_idx_write;
	oct->fn_list.bar1_idx_read = lio_cn6xxx_bar1_idx_read;

	oct->fn_list.process_interrupt_regs = lio_cn6xxx_process_interrupt_regs;
	oct->fn_list.enable_interrupt = lio_cn6xxx_enable_interrupt;
	oct->fn_list.disable_interrupt = lio_cn6xxx_disable_interrupt;

	oct->fn_list.enable_io_queues = lio_cn6xxx_enable_io_queues;
	oct->fn_list.disable_io_queues = lio_cn6xxx_disable_io_queues;

	lio_cn6xxx_setup_reg_address(oct, oct->chip, &oct->reg_list);

	cn6xxx->conf = (struct octeon_config *)
		       oct_get_config_info(oct, LIO_210SV);
	if (!cn6xxx->conf) {
		dev_err(&oct->pci_dev->dev, "%s No Config found for CN66XX\n",
			__func__);
		octeon_unmap_pci_barx(oct, 0);
		octeon_unmap_pci_barx(oct, 1);
		return 1;
	}

	oct->coproc_clock_rate = 1000000ULL * lio_cn6xxx_coprocessor_clock(oct);

	return 0;
}

int lio_validate_cn6xxx_config_info(struct octeon_device *oct,
				    struct octeon_config *conf6xxx)
{
	if (CFG_GET_IQ_MAX_Q(conf6xxx) > CN6XXX_MAX_INPUT_QUEUES) {
		dev_err(&oct->pci_dev->dev, "%s: Num IQ (%d) exceeds Max (%d)\n",
			__func__, CFG_GET_IQ_MAX_Q(conf6xxx),
			CN6XXX_MAX_INPUT_QUEUES);
		return 1;
	}

	if (CFG_GET_OQ_MAX_Q(conf6xxx) > CN6XXX_MAX_OUTPUT_QUEUES) {
		dev_err(&oct->pci_dev->dev, "%s: Num OQ (%d) exceeds Max (%d)\n",
			__func__, CFG_GET_OQ_MAX_Q(conf6xxx),
			CN6XXX_MAX_OUTPUT_QUEUES);
		return 1;
	}

	if (CFG_GET_IQ_INSTR_TYPE(conf6xxx) != OCTEON_32BYTE_INSTR &&
	    CFG_GET_IQ_INSTR_TYPE(conf6xxx) != OCTEON_64BYTE_INSTR) {
		dev_err(&oct->pci_dev->dev, "%s: Invalid instr type for IQ\n",
			__func__);
		return 1;
	}
	if (!CFG_GET_OQ_REFILL_THRESHOLD(conf6xxx)) {
		dev_err(&oct->pci_dev->dev, "%s: Invalid parameter for OQ\n",
			__func__);
		return 1;
	}

	if (!(CFG_GET_OQ_INTR_TIME(conf6xxx))) {
		dev_err(&oct->pci_dev->dev, "%s: No Time Interrupt for OQ\n",
			__func__);
		return 1;
	}

	return 0;
}
