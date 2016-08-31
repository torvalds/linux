/**********************************************************************
* Author: Cavium, Inc.
*
* Contact: support@cavium.com
*          Please include "LiquidIO" in the subject.
*
* Copyright (c) 2003-2015 Cavium, Inc.
*
* This file is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License, Version 2, as
* published by the Free Software Foundation.
*
* This file is distributed in the hope that it will be useful, but
* AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty
* of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
* NONINFRINGEMENT.  See the GNU General Public License for more
* details.
*
* This file may also be available under a different license from Cavium.
* Contact Cavium, Inc. for more information
**********************************************************************/

#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/vmalloc.h>
#include "liquidio_common.h"
#include "octeon_droq.h"
#include "octeon_iq.h"
#include "response_manager.h"
#include "octeon_device.h"
#include "cn23xx_pf_device.h"
#include "octeon_main.h"

#define RESET_NOTDONE 0
#define RESET_DONE 1

/* Change the value of SLI Packet Input Jabber Register to allow
 * VXLAN TSO packets which can be 64424 bytes, exceeding the
 * MAX_GSO_SIZE we supplied to the kernel
 */
#define CN23XX_INPUT_JABBER 64600

#define LIOLUT_RING_DISTRIBUTION 9
const int liolut_num_vfs_to_rings_per_vf[LIOLUT_RING_DISTRIBUTION] = {
	0, 8, 4, 2, 2, 2, 1, 1, 1
};

void cn23xx_dump_pf_initialized_regs(struct octeon_device *oct)
{
	int i = 0;
	u32 regval = 0;
	struct octeon_cn23xx_pf *cn23xx = (struct octeon_cn23xx_pf *)oct->chip;

	/*In cn23xx_soft_reset*/
	dev_dbg(&oct->pci_dev->dev, "%s[%llx] : 0x%llx\n",
		"CN23XX_WIN_WR_MASK_REG", CVM_CAST64(CN23XX_WIN_WR_MASK_REG),
		CVM_CAST64(octeon_read_csr64(oct, CN23XX_WIN_WR_MASK_REG)));
	dev_dbg(&oct->pci_dev->dev, "%s[%llx] : 0x%016llx\n",
		"CN23XX_SLI_SCRATCH1", CVM_CAST64(CN23XX_SLI_SCRATCH1),
		CVM_CAST64(octeon_read_csr64(oct, CN23XX_SLI_SCRATCH1)));
	dev_dbg(&oct->pci_dev->dev, "%s[%llx] : 0x%016llx\n",
		"CN23XX_RST_SOFT_RST", CN23XX_RST_SOFT_RST,
		lio_pci_readq(oct, CN23XX_RST_SOFT_RST));

	/*In cn23xx_set_dpi_regs*/
	dev_dbg(&oct->pci_dev->dev, "%s[%llx] : 0x%016llx\n",
		"CN23XX_DPI_DMA_CONTROL", CN23XX_DPI_DMA_CONTROL,
		lio_pci_readq(oct, CN23XX_DPI_DMA_CONTROL));

	for (i = 0; i < 6; i++) {
		dev_dbg(&oct->pci_dev->dev, "%s(%d)[%llx] : 0x%016llx\n",
			"CN23XX_DPI_DMA_ENG_ENB", i,
			CN23XX_DPI_DMA_ENG_ENB(i),
			lio_pci_readq(oct, CN23XX_DPI_DMA_ENG_ENB(i)));
		dev_dbg(&oct->pci_dev->dev, "%s(%d)[%llx] : 0x%016llx\n",
			"CN23XX_DPI_DMA_ENG_BUF", i,
			CN23XX_DPI_DMA_ENG_BUF(i),
			lio_pci_readq(oct, CN23XX_DPI_DMA_ENG_BUF(i)));
	}

	dev_dbg(&oct->pci_dev->dev, "%s[%llx] : 0x%016llx\n", "CN23XX_DPI_CTL",
		CN23XX_DPI_CTL, lio_pci_readq(oct, CN23XX_DPI_CTL));

	/*In cn23xx_setup_pcie_mps and cn23xx_setup_pcie_mrrs */
	pci_read_config_dword(oct->pci_dev, CN23XX_CONFIG_PCIE_DEVCTL, &regval);
	dev_dbg(&oct->pci_dev->dev, "%s[%llx] : 0x%016llx\n",
		"CN23XX_CONFIG_PCIE_DEVCTL",
		CVM_CAST64(CN23XX_CONFIG_PCIE_DEVCTL), CVM_CAST64(regval));

	dev_dbg(&oct->pci_dev->dev, "%s(%d)[%llx] : 0x%016llx\n",
		"CN23XX_DPI_SLI_PRTX_CFG", oct->pcie_port,
		CN23XX_DPI_SLI_PRTX_CFG(oct->pcie_port),
		lio_pci_readq(oct, CN23XX_DPI_SLI_PRTX_CFG(oct->pcie_port)));

	/*In cn23xx_specific_regs_setup */
	dev_dbg(&oct->pci_dev->dev, "%s(%d)[%llx] : 0x%016llx\n",
		"CN23XX_SLI_S2M_PORTX_CTL", oct->pcie_port,
		CVM_CAST64(CN23XX_SLI_S2M_PORTX_CTL(oct->pcie_port)),
		CVM_CAST64(octeon_read_csr64(
			oct, CN23XX_SLI_S2M_PORTX_CTL(oct->pcie_port))));

	dev_dbg(&oct->pci_dev->dev, "%s[%llx] : 0x%016llx\n",
		"CN23XX_SLI_RING_RST", CVM_CAST64(CN23XX_SLI_PKT_IOQ_RING_RST),
		(u64)octeon_read_csr64(oct, CN23XX_SLI_PKT_IOQ_RING_RST));

	/*In cn23xx_setup_global_mac_regs*/
	for (i = 0; i < CN23XX_MAX_MACS; i++) {
		dev_dbg(&oct->pci_dev->dev, "%s(%d)[%llx] : 0x%016llx\n",
			"CN23XX_SLI_PKT_MAC_RINFO64", i,
			CVM_CAST64(CN23XX_SLI_PKT_MAC_RINFO64(i, oct->pf_num)),
			CVM_CAST64(octeon_read_csr64
				(oct, CN23XX_SLI_PKT_MAC_RINFO64
					(i, oct->pf_num))));
	}

	/*In cn23xx_setup_global_input_regs*/
	for (i = 0; i < CN23XX_MAX_INPUT_QUEUES; i++) {
		dev_dbg(&oct->pci_dev->dev, "%s(%d)[%llx] : 0x%016llx\n",
			"CN23XX_SLI_IQ_PKT_CONTROL64", i,
			CVM_CAST64(CN23XX_SLI_IQ_PKT_CONTROL64(i)),
			CVM_CAST64(octeon_read_csr64
				(oct, CN23XX_SLI_IQ_PKT_CONTROL64(i))));
	}

	/*In cn23xx_setup_global_output_regs*/
	dev_dbg(&oct->pci_dev->dev, "%s[%llx] : 0x%016llx\n",
		"CN23XX_SLI_OQ_WMARK", CVM_CAST64(CN23XX_SLI_OQ_WMARK),
		CVM_CAST64(octeon_read_csr64(oct, CN23XX_SLI_OQ_WMARK)));

	for (i = 0; i < CN23XX_MAX_OUTPUT_QUEUES; i++) {
		dev_dbg(&oct->pci_dev->dev, "%s(%d)[%llx] : 0x%016llx\n",
			"CN23XX_SLI_OQ_PKT_CONTROL", i,
			CVM_CAST64(CN23XX_SLI_OQ_PKT_CONTROL(i)),
			CVM_CAST64(octeon_read_csr(
				oct, CN23XX_SLI_OQ_PKT_CONTROL(i))));
		dev_dbg(&oct->pci_dev->dev, "%s(%d)[%llx] : 0x%016llx\n",
			"CN23XX_SLI_OQ_PKT_INT_LEVELS", i,
			CVM_CAST64(CN23XX_SLI_OQ_PKT_INT_LEVELS(i)),
			CVM_CAST64(octeon_read_csr64(
				oct, CN23XX_SLI_OQ_PKT_INT_LEVELS(i))));
	}

	/*In cn23xx_enable_interrupt and cn23xx_disable_interrupt*/
	dev_dbg(&oct->pci_dev->dev, "%s[%llx] : 0x%016llx\n",
		"cn23xx->intr_enb_reg64",
		CVM_CAST64((long)(cn23xx->intr_enb_reg64)),
		CVM_CAST64(readq(cn23xx->intr_enb_reg64)));

	dev_dbg(&oct->pci_dev->dev, "%s[%llx] : 0x%016llx\n",
		"cn23xx->intr_sum_reg64",
		CVM_CAST64((long)(cn23xx->intr_sum_reg64)),
		CVM_CAST64(readq(cn23xx->intr_sum_reg64)));

	/*In cn23xx_setup_iq_regs*/
	for (i = 0; i < CN23XX_MAX_INPUT_QUEUES; i++) {
		dev_dbg(&oct->pci_dev->dev, "%s(%d)[%llx] : 0x%016llx\n",
			"CN23XX_SLI_IQ_BASE_ADDR64", i,
			CVM_CAST64(CN23XX_SLI_IQ_BASE_ADDR64(i)),
			CVM_CAST64(octeon_read_csr64(
				oct, CN23XX_SLI_IQ_BASE_ADDR64(i))));
		dev_dbg(&oct->pci_dev->dev, "%s(%d)[%llx] : 0x%016llx\n",
			"CN23XX_SLI_IQ_SIZE", i,
			CVM_CAST64(CN23XX_SLI_IQ_SIZE(i)),
			CVM_CAST64(octeon_read_csr
				(oct, CN23XX_SLI_IQ_SIZE(i))));
		dev_dbg(&oct->pci_dev->dev, "%s(%d)[%llx] : 0x%016llx\n",
			"CN23XX_SLI_IQ_DOORBELL", i,
			CVM_CAST64(CN23XX_SLI_IQ_DOORBELL(i)),
			CVM_CAST64(octeon_read_csr64(
				oct, CN23XX_SLI_IQ_DOORBELL(i))));
		dev_dbg(&oct->pci_dev->dev, "%s(%d)[%llx] : 0x%016llx\n",
			"CN23XX_SLI_IQ_INSTR_COUNT64", i,
			CVM_CAST64(CN23XX_SLI_IQ_INSTR_COUNT64(i)),
			CVM_CAST64(octeon_read_csr64(
				oct, CN23XX_SLI_IQ_INSTR_COUNT64(i))));
	}

	/*In cn23xx_setup_oq_regs*/
	for (i = 0; i < CN23XX_MAX_OUTPUT_QUEUES; i++) {
		dev_dbg(&oct->pci_dev->dev, "%s(%d)[%llx] : 0x%016llx\n",
			"CN23XX_SLI_OQ_BASE_ADDR64", i,
			CVM_CAST64(CN23XX_SLI_OQ_BASE_ADDR64(i)),
			CVM_CAST64(octeon_read_csr64(
				oct, CN23XX_SLI_OQ_BASE_ADDR64(i))));
		dev_dbg(&oct->pci_dev->dev, "%s(%d)[%llx] : 0x%016llx\n",
			"CN23XX_SLI_OQ_SIZE", i,
			CVM_CAST64(CN23XX_SLI_OQ_SIZE(i)),
			CVM_CAST64(octeon_read_csr
				(oct, CN23XX_SLI_OQ_SIZE(i))));
		dev_dbg(&oct->pci_dev->dev, "%s(%d)[%llx] : 0x%016llx\n",
			"CN23XX_SLI_OQ_BUFF_INFO_SIZE", i,
			CVM_CAST64(CN23XX_SLI_OQ_BUFF_INFO_SIZE(i)),
			CVM_CAST64(octeon_read_csr(
				oct, CN23XX_SLI_OQ_BUFF_INFO_SIZE(i))));
		dev_dbg(&oct->pci_dev->dev, "%s(%d)[%llx] : 0x%016llx\n",
			"CN23XX_SLI_OQ_PKTS_SENT", i,
			CVM_CAST64(CN23XX_SLI_OQ_PKTS_SENT(i)),
			CVM_CAST64(octeon_read_csr64(
				oct, CN23XX_SLI_OQ_PKTS_SENT(i))));
		dev_dbg(&oct->pci_dev->dev, "%s(%d)[%llx] : 0x%016llx\n",
			"CN23XX_SLI_OQ_PKTS_CREDIT", i,
			CVM_CAST64(CN23XX_SLI_OQ_PKTS_CREDIT(i)),
			CVM_CAST64(octeon_read_csr64(
				oct, CN23XX_SLI_OQ_PKTS_CREDIT(i))));
	}

	dev_dbg(&oct->pci_dev->dev, "%s[%llx] : 0x%016llx\n",
		"CN23XX_SLI_PKT_TIME_INT",
		CVM_CAST64(CN23XX_SLI_PKT_TIME_INT),
		CVM_CAST64(octeon_read_csr64(oct, CN23XX_SLI_PKT_TIME_INT)));
	dev_dbg(&oct->pci_dev->dev, "%s[%llx] : 0x%016llx\n",
		"CN23XX_SLI_PKT_CNT_INT",
		CVM_CAST64(CN23XX_SLI_PKT_CNT_INT),
		CVM_CAST64(octeon_read_csr64(oct, CN23XX_SLI_PKT_CNT_INT)));
}

static u32 cn23xx_coprocessor_clock(struct octeon_device *oct)
{
	/* Bits 29:24 of RST_BOOT[PNR_MUL] holds the ref.clock MULTIPLIER
	 * for SLI.
	 */

	/* TBD: get the info in Hand-shake */
	return (((lio_pci_readq(oct, CN23XX_RST_BOOT) >> 24) & 0x3f) * 50);
}

static void cn23xx_setup_iq_regs(struct octeon_device *oct, u32 iq_no)
{
	struct octeon_instr_queue *iq = oct->instr_queue[iq_no];
	u64 pkt_in_done;

	iq_no += oct->sriov_info.pf_srn;

	/* Write the start of the input queue's ring and its size  */
	octeon_write_csr64(oct, CN23XX_SLI_IQ_BASE_ADDR64(iq_no),
			   iq->base_addr_dma);
	octeon_write_csr(oct, CN23XX_SLI_IQ_SIZE(iq_no), iq->max_count);

	/* Remember the doorbell & instruction count register addr
	 * for this queue
	 */
	iq->doorbell_reg =
	    (u8 *)oct->mmio[0].hw_addr + CN23XX_SLI_IQ_DOORBELL(iq_no);
	iq->inst_cnt_reg =
	    (u8 *)oct->mmio[0].hw_addr + CN23XX_SLI_IQ_INSTR_COUNT64(iq_no);
	dev_dbg(&oct->pci_dev->dev, "InstQ[%d]:dbell reg @ 0x%p instcnt_reg @ 0x%p\n",
		iq_no, iq->doorbell_reg, iq->inst_cnt_reg);

	/* Store the current instruction counter (used in flush_iq
	 * calculation)
	 */
	pkt_in_done = readq(iq->inst_cnt_reg);

	/* Clear the count by writing back what we read, but don't
	 * enable interrupts
	 */
	writeq(pkt_in_done, iq->inst_cnt_reg);

	iq->reset_instr_cnt = 0;
}

static void cn23xx_setup_oq_regs(struct octeon_device *oct, u32 oq_no)
{
	u32 reg_val;
	struct octeon_droq *droq = oct->droq[oq_no];

	oq_no += oct->sriov_info.pf_srn;

	octeon_write_csr64(oct, CN23XX_SLI_OQ_BASE_ADDR64(oq_no),
			   droq->desc_ring_dma);
	octeon_write_csr(oct, CN23XX_SLI_OQ_SIZE(oq_no), droq->max_count);

	octeon_write_csr(oct, CN23XX_SLI_OQ_BUFF_INFO_SIZE(oq_no),
			 (droq->buffer_size | (OCT_RH_SIZE << 16)));

	/* Get the mapped address of the pkt_sent and pkts_credit regs */
	droq->pkts_sent_reg =
	    (u8 *)oct->mmio[0].hw_addr + CN23XX_SLI_OQ_PKTS_SENT(oq_no);
	droq->pkts_credit_reg =
	    (u8 *)oct->mmio[0].hw_addr + CN23XX_SLI_OQ_PKTS_CREDIT(oq_no);

	/* Enable this output queue to generate Packet Timer Interrupt
	*/
	reg_val = octeon_read_csr(oct, CN23XX_SLI_OQ_PKT_CONTROL(oq_no));
	reg_val |= CN23XX_PKT_OUTPUT_CTL_TENB;
	octeon_write_csr(oct, CN23XX_SLI_OQ_PKT_CONTROL(oq_no),
			 reg_val);

	/* Enable this output queue to generate Packet Count Interrupt
	*/
	reg_val = octeon_read_csr(oct, CN23XX_SLI_OQ_PKT_CONTROL(oq_no));
	reg_val |= CN23XX_PKT_OUTPUT_CTL_CENB;
	octeon_write_csr(oct, CN23XX_SLI_OQ_PKT_CONTROL(oq_no),
			 reg_val);
}

static void cn23xx_get_pcie_qlmport(struct octeon_device *oct)
{
	oct->pcie_port = (octeon_read_csr(oct, CN23XX_SLI_MAC_NUMBER)) & 0xff;

	dev_dbg(&oct->pci_dev->dev, "OCTEON: CN23xx uses PCIE Port %d\n",
		oct->pcie_port);
}

static void cn23xx_get_pf_num(struct octeon_device *oct)
{
	u32 fdl_bit = 0;

	/** Read Function Dependency Link reg to get the function number */
	pci_read_config_dword(oct->pci_dev, CN23XX_PCIE_SRIOV_FDL, &fdl_bit);
	oct->pf_num = ((fdl_bit >> CN23XX_PCIE_SRIOV_FDL_BIT_POS) &
		       CN23XX_PCIE_SRIOV_FDL_MASK);
}

static void cn23xx_setup_reg_address(struct octeon_device *oct)
{
	u8 __iomem *bar0_pciaddr = oct->mmio[0].hw_addr;
	struct octeon_cn23xx_pf *cn23xx = (struct octeon_cn23xx_pf *)oct->chip;

	oct->reg_list.pci_win_wr_addr_hi =
	    (u32 __iomem *)(bar0_pciaddr + CN23XX_WIN_WR_ADDR_HI);
	oct->reg_list.pci_win_wr_addr_lo =
	    (u32 __iomem *)(bar0_pciaddr + CN23XX_WIN_WR_ADDR_LO);
	oct->reg_list.pci_win_wr_addr =
	    (u64 __iomem *)(bar0_pciaddr + CN23XX_WIN_WR_ADDR64);

	oct->reg_list.pci_win_rd_addr_hi =
	    (u32 __iomem *)(bar0_pciaddr + CN23XX_WIN_RD_ADDR_HI);
	oct->reg_list.pci_win_rd_addr_lo =
	    (u32 __iomem *)(bar0_pciaddr + CN23XX_WIN_RD_ADDR_LO);
	oct->reg_list.pci_win_rd_addr =
	    (u64 __iomem *)(bar0_pciaddr + CN23XX_WIN_RD_ADDR64);

	oct->reg_list.pci_win_wr_data_hi =
	    (u32 __iomem *)(bar0_pciaddr + CN23XX_WIN_WR_DATA_HI);
	oct->reg_list.pci_win_wr_data_lo =
	    (u32 __iomem *)(bar0_pciaddr + CN23XX_WIN_WR_DATA_LO);
	oct->reg_list.pci_win_wr_data =
	    (u64 __iomem *)(bar0_pciaddr + CN23XX_WIN_WR_DATA64);

	oct->reg_list.pci_win_rd_data_hi =
	    (u32 __iomem *)(bar0_pciaddr + CN23XX_WIN_RD_DATA_HI);
	oct->reg_list.pci_win_rd_data_lo =
	    (u32 __iomem *)(bar0_pciaddr + CN23XX_WIN_RD_DATA_LO);
	oct->reg_list.pci_win_rd_data =
	    (u64 __iomem *)(bar0_pciaddr + CN23XX_WIN_RD_DATA64);

	cn23xx_get_pcie_qlmport(oct);

	cn23xx->intr_mask64 = CN23XX_INTR_MASK;
	cn23xx->intr_mask64 |= CN23XX_INTR_PKT_TIME;
	if (oct->rev_id >= OCTEON_CN23XX_REV_1_1)
		cn23xx->intr_mask64 |= CN23XX_INTR_VF_MBOX;

	cn23xx->intr_sum_reg64 =
	    bar0_pciaddr +
	    CN23XX_SLI_MAC_PF_INT_SUM64(oct->pcie_port, oct->pf_num);
	cn23xx->intr_enb_reg64 =
	    bar0_pciaddr +
	    CN23XX_SLI_MAC_PF_INT_ENB64(oct->pcie_port, oct->pf_num);
}

static int cn23xx_sriov_config(struct octeon_device *oct)
{
	u32 total_rings;
	struct octeon_cn23xx_pf *cn23xx = (struct octeon_cn23xx_pf *)oct->chip;
	/* num_vfs is already filled for us */
	u32 pf_srn, num_pf_rings;

	cn23xx->conf =
	    (struct octeon_config *)oct_get_config_info(oct, LIO_23XX);
	switch (oct->rev_id) {
	case OCTEON_CN23XX_REV_1_0:
		total_rings = CN23XX_MAX_RINGS_PER_PF_PASS_1_0;
		break;
	case OCTEON_CN23XX_REV_1_1:
		total_rings = CN23XX_MAX_RINGS_PER_PF_PASS_1_1;
		break;
	default:
		total_rings = CN23XX_MAX_RINGS_PER_PF;
		break;
	}
	if (!oct->sriov_info.num_pf_rings) {
		if (total_rings > num_present_cpus())
			num_pf_rings = num_present_cpus();
		else
			num_pf_rings = total_rings;
	} else {
		num_pf_rings = oct->sriov_info.num_pf_rings;

		if (num_pf_rings > total_rings) {
			dev_warn(&oct->pci_dev->dev,
				 "num_queues_per_pf requested %u is more than available rings. Reducing to %u\n",
				 num_pf_rings, total_rings);
			num_pf_rings = total_rings;
		}
	}

	total_rings = num_pf_rings;
	/* the first ring of the pf */
	pf_srn = total_rings - num_pf_rings;

	oct->sriov_info.trs = total_rings;
	oct->sriov_info.pf_srn = pf_srn;
	oct->sriov_info.num_pf_rings = num_pf_rings;
	dev_dbg(&oct->pci_dev->dev, "trs:%d pf_srn:%d num_pf_rings:%d\n",
		oct->sriov_info.trs, oct->sriov_info.pf_srn,
		oct->sriov_info.num_pf_rings);
	return 0;
}

int setup_cn23xx_octeon_pf_device(struct octeon_device *oct)
{
	if (octeon_map_pci_barx(oct, 0, 0))
		return 1;

	if (octeon_map_pci_barx(oct, 1, MAX_BAR1_IOREMAP_SIZE)) {
		dev_err(&oct->pci_dev->dev, "%s CN23XX BAR1 map failed\n",
			__func__);
		octeon_unmap_pci_barx(oct, 0);
		return 1;
	}

	cn23xx_get_pf_num(oct);

	if (cn23xx_sriov_config(oct)) {
		octeon_unmap_pci_barx(oct, 0);
		octeon_unmap_pci_barx(oct, 1);
		return 1;
	}

	octeon_write_csr64(oct, CN23XX_SLI_MAC_CREDIT_CNT, 0x3F802080802080ULL);

	oct->fn_list.setup_iq_regs = cn23xx_setup_iq_regs;
	oct->fn_list.setup_oq_regs = cn23xx_setup_oq_regs;

	cn23xx_setup_reg_address(oct);

	oct->coproc_clock_rate = 1000000ULL * cn23xx_coprocessor_clock(oct);

	return 0;
}

int validate_cn23xx_pf_config_info(struct octeon_device *oct,
				   struct octeon_config *conf23xx)
{
	if (CFG_GET_IQ_MAX_Q(conf23xx) > CN23XX_MAX_INPUT_QUEUES) {
		dev_err(&oct->pci_dev->dev, "%s: Num IQ (%d) exceeds Max (%d)\n",
			__func__, CFG_GET_IQ_MAX_Q(conf23xx),
			CN23XX_MAX_INPUT_QUEUES);
		return 1;
	}

	if (CFG_GET_OQ_MAX_Q(conf23xx) > CN23XX_MAX_OUTPUT_QUEUES) {
		dev_err(&oct->pci_dev->dev, "%s: Num OQ (%d) exceeds Max (%d)\n",
			__func__, CFG_GET_OQ_MAX_Q(conf23xx),
			CN23XX_MAX_OUTPUT_QUEUES);
		return 1;
	}

	if (CFG_GET_IQ_INSTR_TYPE(conf23xx) != OCTEON_32BYTE_INSTR &&
	    CFG_GET_IQ_INSTR_TYPE(conf23xx) != OCTEON_64BYTE_INSTR) {
		dev_err(&oct->pci_dev->dev, "%s: Invalid instr type for IQ\n",
			__func__);
		return 1;
	}

	if (!(CFG_GET_OQ_INFO_PTR(conf23xx)) ||
	    !(CFG_GET_OQ_REFILL_THRESHOLD(conf23xx))) {
		dev_err(&oct->pci_dev->dev, "%s: Invalid parameter for OQ\n",
			__func__);
		return 1;
	}

	if (!(CFG_GET_OQ_INTR_TIME(conf23xx))) {
		dev_err(&oct->pci_dev->dev, "%s: Invalid parameter for OQ\n",
			__func__);
		return 1;
	}

	return 0;
}

void cn23xx_dump_iq_regs(struct octeon_device *oct)
{
	u32 regval, q_no;

	dev_dbg(&oct->pci_dev->dev, "SLI_IQ_DOORBELL_0 [0x%x]: 0x%016llx\n",
		CN23XX_SLI_IQ_DOORBELL(0),
		CVM_CAST64(octeon_read_csr64
			(oct, CN23XX_SLI_IQ_DOORBELL(0))));

	dev_dbg(&oct->pci_dev->dev, "SLI_IQ_BASEADDR_0 [0x%x]: 0x%016llx\n",
		CN23XX_SLI_IQ_BASE_ADDR64(0),
		CVM_CAST64(octeon_read_csr64
			(oct, CN23XX_SLI_IQ_BASE_ADDR64(0))));

	dev_dbg(&oct->pci_dev->dev, "SLI_IQ_FIFO_RSIZE_0 [0x%x]: 0x%016llx\n",
		CN23XX_SLI_IQ_SIZE(0),
		CVM_CAST64(octeon_read_csr64(oct, CN23XX_SLI_IQ_SIZE(0))));

	dev_dbg(&oct->pci_dev->dev, "SLI_CTL_STATUS [0x%x]: 0x%016llx\n",
		CN23XX_SLI_CTL_STATUS,
		CVM_CAST64(octeon_read_csr64(oct, CN23XX_SLI_CTL_STATUS)));

	for (q_no = 0; q_no < CN23XX_MAX_INPUT_QUEUES; q_no++) {
		dev_dbg(&oct->pci_dev->dev, "SLI_PKT[%d]_INPUT_CTL [0x%x]: 0x%016llx\n",
			q_no, CN23XX_SLI_IQ_PKT_CONTROL64(q_no),
			CVM_CAST64(octeon_read_csr64
				(oct,
					CN23XX_SLI_IQ_PKT_CONTROL64(q_no))));
	}

	pci_read_config_dword(oct->pci_dev, CN23XX_CONFIG_PCIE_DEVCTL, &regval);
	dev_dbg(&oct->pci_dev->dev, "Config DevCtl [0x%x]: 0x%08x\n",
		CN23XX_CONFIG_PCIE_DEVCTL, regval);

	dev_dbg(&oct->pci_dev->dev, "SLI_PRT[%d]_CFG [0x%llx]: 0x%016llx\n",
		oct->pcie_port, CN23XX_DPI_SLI_PRTX_CFG(oct->pcie_port),
		CVM_CAST64(lio_pci_readq(
			oct, CN23XX_DPI_SLI_PRTX_CFG(oct->pcie_port))));

	dev_dbg(&oct->pci_dev->dev, "SLI_S2M_PORT[%d]_CTL [0x%x]: 0x%016llx\n",
		oct->pcie_port, CN23XX_SLI_S2M_PORTX_CTL(oct->pcie_port),
		CVM_CAST64(octeon_read_csr64(
			oct, CN23XX_SLI_S2M_PORTX_CTL(oct->pcie_port))));
}
