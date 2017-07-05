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

static int cn23xx_pf_soft_reset(struct octeon_device *oct)
{
	octeon_write_csr64(oct, CN23XX_WIN_WR_MASK_REG, 0xFF);

	dev_dbg(&oct->pci_dev->dev, "OCTEON[%d]: BIST enabled for CN23XX soft reset\n",
		oct->octeon_id);

	octeon_write_csr64(oct, CN23XX_SLI_SCRATCH1, 0x1234ULL);

	/* Initiate chip-wide soft reset */
	lio_pci_readq(oct, CN23XX_RST_SOFT_RST);
	lio_pci_writeq(oct, 1, CN23XX_RST_SOFT_RST);

	/* Wait for 100ms as Octeon resets. */
	mdelay(100);

	if (octeon_read_csr64(oct, CN23XX_SLI_SCRATCH1)) {
		dev_err(&oct->pci_dev->dev, "OCTEON[%d]: Soft reset failed\n",
			oct->octeon_id);
		return 1;
	}

	dev_dbg(&oct->pci_dev->dev, "OCTEON[%d]: Reset completed\n",
		oct->octeon_id);

	/* restore the  reset value*/
	octeon_write_csr64(oct, CN23XX_WIN_WR_MASK_REG, 0xFF);

	return 0;
}

static void cn23xx_enable_error_reporting(struct octeon_device *oct)
{
	u32 regval;
	u32 uncorrectable_err_mask, corrtable_err_status;

	pci_read_config_dword(oct->pci_dev, CN23XX_CONFIG_PCIE_DEVCTL, &regval);
	if (regval & CN23XX_CONFIG_PCIE_DEVCTL_MASK) {
		uncorrectable_err_mask = 0;
		corrtable_err_status = 0;
		pci_read_config_dword(oct->pci_dev,
				      CN23XX_CONFIG_PCIE_UNCORRECT_ERR_MASK,
				      &uncorrectable_err_mask);
		pci_read_config_dword(oct->pci_dev,
				      CN23XX_CONFIG_PCIE_CORRECT_ERR_STATUS,
				      &corrtable_err_status);
		dev_err(&oct->pci_dev->dev, "PCI-E Fatal error detected;\n"
				 "\tdev_ctl_status_reg = 0x%08x\n"
				 "\tuncorrectable_error_mask_reg = 0x%08x\n"
				 "\tcorrectable_error_status_reg = 0x%08x\n",
			    regval, uncorrectable_err_mask,
			    corrtable_err_status);
	}

	regval |= 0xf; /* Enable Link error reporting */

	dev_dbg(&oct->pci_dev->dev, "OCTEON[%d]: Enabling PCI-E error reporting..\n",
		oct->octeon_id);
	pci_write_config_dword(oct->pci_dev, CN23XX_CONFIG_PCIE_DEVCTL, regval);
}

static u32 cn23xx_coprocessor_clock(struct octeon_device *oct)
{
	/* Bits 29:24 of RST_BOOT[PNR_MUL] holds the ref.clock MULTIPLIER
	 * for SLI.
	 */

	/* TBD: get the info in Hand-shake */
	return (((lio_pci_readq(oct, CN23XX_RST_BOOT) >> 24) & 0x3f) * 50);
}

u32 cn23xx_pf_get_oq_ticks(struct octeon_device *oct, u32 time_intr_in_us)
{
	/* This gives the SLI clock per microsec */
	u32 oqticks_per_us = cn23xx_coprocessor_clock(oct);

	oct->pfvf_hsword.coproc_tics_per_us = oqticks_per_us;

	/* This gives the clock cycles per millisecond */
	oqticks_per_us *= 1000;

	/* This gives the oq ticks (1024 core clock cycles) per millisecond */
	oqticks_per_us /= 1024;

	/* time_intr is in microseconds. The next 2 steps gives the oq ticks
	 *  corressponding to time_intr.
	 */
	oqticks_per_us *= time_intr_in_us;
	oqticks_per_us /= 1000;

	return oqticks_per_us;
}

static void cn23xx_setup_global_mac_regs(struct octeon_device *oct)
{
	u64 reg_val;
	u16 mac_no = oct->pcie_port;
	u16 pf_num = oct->pf_num;

	/* programming SRN and TRS for each MAC(0..3)  */

	dev_dbg(&oct->pci_dev->dev, "%s:Using pcie port %d\n",
		__func__, mac_no);
	/* By default, mapping all 64 IOQs to  a single MACs */

	reg_val =
	    octeon_read_csr64(oct, CN23XX_SLI_PKT_MAC_RINFO64(mac_no, pf_num));

	if (oct->rev_id == OCTEON_CN23XX_REV_1_1) {
		/* setting SRN <6:0>  */
		reg_val = pf_num * CN23XX_MAX_RINGS_PER_PF_PASS_1_1;
	} else {
		/* setting SRN <6:0>  */
		reg_val = pf_num * CN23XX_MAX_RINGS_PER_PF;
	}

	/* setting TRS <23:16> */
	reg_val = reg_val |
		  (oct->sriov_info.trs << CN23XX_PKT_MAC_CTL_RINFO_TRS_BIT_POS);
	/* write these settings to MAC register */
	octeon_write_csr64(oct, CN23XX_SLI_PKT_MAC_RINFO64(mac_no, pf_num),
			   reg_val);

	dev_dbg(&oct->pci_dev->dev, "SLI_PKT_MAC(%d)_PF(%d)_RINFO : 0x%016llx\n",
		mac_no, pf_num, (u64)octeon_read_csr64
		(oct, CN23XX_SLI_PKT_MAC_RINFO64(mac_no, pf_num)));
}

static int cn23xx_reset_io_queues(struct octeon_device *oct)
{
	int ret_val = 0;
	u64 d64;
	u32 q_no, srn, ern;
	u32 loop = 1000;

	srn = oct->sriov_info.pf_srn;
	ern = srn + oct->sriov_info.num_pf_rings;

	/*As per HRM reg description, s/w cant write 0 to ENB. */
	/*to make the queue off, need to set the RST bit. */

	/* Reset the Enable bit for all the 64 IQs.  */
	for (q_no = srn; q_no < ern; q_no++) {
		/* set RST bit to 1. This bit applies to both IQ and OQ */
		d64 = octeon_read_csr64(oct, CN23XX_SLI_IQ_PKT_CONTROL64(q_no));
		d64 = d64 | CN23XX_PKT_INPUT_CTL_RST;
		octeon_write_csr64(oct, CN23XX_SLI_IQ_PKT_CONTROL64(q_no), d64);
	}

	/*wait until the RST bit is clear or the RST and quite bits are set*/
	for (q_no = srn; q_no < ern; q_no++) {
		u64 reg_val = octeon_read_csr64(oct,
					CN23XX_SLI_IQ_PKT_CONTROL64(q_no));
		while ((READ_ONCE(reg_val) & CN23XX_PKT_INPUT_CTL_RST) &&
		       !(READ_ONCE(reg_val) & CN23XX_PKT_INPUT_CTL_QUIET) &&
		       loop--) {
			WRITE_ONCE(reg_val, octeon_read_csr64(
			    oct, CN23XX_SLI_IQ_PKT_CONTROL64(q_no)));
		}
		if (!loop) {
			dev_err(&oct->pci_dev->dev,
				"clearing the reset reg failed or setting the quiet reg failed for qno: %u\n",
				q_no);
			return -1;
		}
		WRITE_ONCE(reg_val, READ_ONCE(reg_val) &
			~CN23XX_PKT_INPUT_CTL_RST);
		octeon_write_csr64(oct, CN23XX_SLI_IQ_PKT_CONTROL64(q_no),
				   READ_ONCE(reg_val));

		WRITE_ONCE(reg_val, octeon_read_csr64(
			   oct, CN23XX_SLI_IQ_PKT_CONTROL64(q_no)));
		if (READ_ONCE(reg_val) & CN23XX_PKT_INPUT_CTL_RST) {
			dev_err(&oct->pci_dev->dev,
				"clearing the reset failed for qno: %u\n",
				q_no);
			ret_val = -1;
		}
	}

	return ret_val;
}

static int cn23xx_pf_setup_global_input_regs(struct octeon_device *oct)
{
	u32 q_no, ern, srn;
	u64 pf_num;
	u64 intr_threshold, reg_val;
	struct octeon_instr_queue *iq;
	struct octeon_cn23xx_pf *cn23xx = (struct octeon_cn23xx_pf *)oct->chip;

	pf_num = oct->pf_num;

	srn = oct->sriov_info.pf_srn;
	ern = srn + oct->sriov_info.num_pf_rings;

	if (cn23xx_reset_io_queues(oct))
		return -1;

	/** Set the MAC_NUM and PVF_NUM in IQ_PKT_CONTROL reg
	* for all queues.Only PF can set these bits.
	* bits 29:30 indicate the MAC num.
	* bits 32:47 indicate the PVF num.
	*/
	for (q_no = 0; q_no < ern; q_no++) {
		reg_val = oct->pcie_port << CN23XX_PKT_INPUT_CTL_MAC_NUM_POS;
		reg_val |= pf_num << CN23XX_PKT_INPUT_CTL_PF_NUM_POS;

		octeon_write_csr64(oct, CN23XX_SLI_IQ_PKT_CONTROL64(q_no),
				   reg_val);
	}

	/* Select ES, RO, NS, RDSIZE,DPTR Fomat#0 for
	 * pf queues
	 */
	for (q_no = srn; q_no < ern; q_no++) {
		void __iomem *inst_cnt_reg;

		iq = oct->instr_queue[q_no];
		if (iq)
			inst_cnt_reg = iq->inst_cnt_reg;
		else
			inst_cnt_reg = (u8 *)oct->mmio[0].hw_addr +
				       CN23XX_SLI_IQ_INSTR_COUNT64(q_no);

		reg_val =
		    octeon_read_csr64(oct, CN23XX_SLI_IQ_PKT_CONTROL64(q_no));

		reg_val |= CN23XX_PKT_INPUT_CTL_MASK;

		octeon_write_csr64(oct, CN23XX_SLI_IQ_PKT_CONTROL64(q_no),
				   reg_val);

		/* Set WMARK level for triggering PI_INT */
		/* intr_threshold = CN23XX_DEF_IQ_INTR_THRESHOLD & */
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

static void cn23xx_pf_setup_global_output_regs(struct octeon_device *oct)
{
	u32 reg_val;
	u32 q_no, ern, srn;
	u64 time_threshold;

	struct octeon_cn23xx_pf *cn23xx = (struct octeon_cn23xx_pf *)oct->chip;

	srn = oct->sriov_info.pf_srn;
	ern = srn + oct->sriov_info.num_pf_rings;

	if (CFG_GET_IS_SLI_BP_ON(cn23xx->conf)) {
		octeon_write_csr64(oct, CN23XX_SLI_OQ_WMARK, 32);
	} else {
		/** Set Output queue watermark to 0 to disable backpressure */
		octeon_write_csr64(oct, CN23XX_SLI_OQ_WMARK, 0);
	}

	for (q_no = srn; q_no < ern; q_no++) {
		reg_val = octeon_read_csr(oct, CN23XX_SLI_OQ_PKT_CONTROL(q_no));

		/* set IPTR & DPTR */
		reg_val |=
		    (CN23XX_PKT_OUTPUT_CTL_IPTR | CN23XX_PKT_OUTPUT_CTL_DPTR);

		/* reset BMODE */
		reg_val &= ~(CN23XX_PKT_OUTPUT_CTL_BMODE);

		/* No Relaxed Ordering, No Snoop, 64-bit Byte swap
		 * for Output Queue ScatterList
		 * reset ROR_P, NSR_P
		 */
		reg_val &= ~(CN23XX_PKT_OUTPUT_CTL_ROR_P);
		reg_val &= ~(CN23XX_PKT_OUTPUT_CTL_NSR_P);

#ifdef __LITTLE_ENDIAN_BITFIELD
		reg_val &= ~(CN23XX_PKT_OUTPUT_CTL_ES_P);
#else
		reg_val |= (CN23XX_PKT_OUTPUT_CTL_ES_P);
#endif
		/* No Relaxed Ordering, No Snoop, 64-bit Byte swap
		 * for Output Queue Data
		 * reset ROR, NSR
		 */
		reg_val &= ~(CN23XX_PKT_OUTPUT_CTL_ROR);
		reg_val &= ~(CN23XX_PKT_OUTPUT_CTL_NSR);
		/* set the ES bit */
		reg_val |= (CN23XX_PKT_OUTPUT_CTL_ES);

		/* write all the selected settings */
		octeon_write_csr(oct, CN23XX_SLI_OQ_PKT_CONTROL(q_no), reg_val);

		/* Enabling these interrupt in oct->fn_list.enable_interrupt()
		 * routine which called after IOQ init.
		 * Set up interrupt packet and time thresholds
		 * for all the OQs
		 */
		time_threshold = cn23xx_pf_get_oq_ticks(
		    oct, (u32)CFG_GET_OQ_INTR_TIME(cn23xx->conf));

		octeon_write_csr64(oct, CN23XX_SLI_OQ_PKT_INT_LEVELS(q_no),
				   (CFG_GET_OQ_INTR_PKT(cn23xx->conf) |
				    (time_threshold << 32)));
	}

	/** Setting the water mark level for pko back pressure **/
	writeq(0x40, (u8 *)oct->mmio[0].hw_addr + CN23XX_SLI_OQ_WMARK);

	/** Disabling setting OQs in reset when ring has no dorebells
	  * enabling this will cause of head of line blocking
	  */
	/* Do it only for pass1.1. and pass1.2 */
	if ((oct->rev_id == OCTEON_CN23XX_REV_1_0) ||
	    (oct->rev_id == OCTEON_CN23XX_REV_1_1))
		writeq(readq((u8 *)oct->mmio[0].hw_addr +
				     CN23XX_SLI_GBL_CONTROL) | 0x2,
		       (u8 *)oct->mmio[0].hw_addr + CN23XX_SLI_GBL_CONTROL);

	/** Enable channel-level backpressure */
	if (oct->pf_num)
		writeq(0xffffffffffffffffULL,
		       (u8 *)oct->mmio[0].hw_addr + CN23XX_SLI_OUT_BP_EN2_W1S);
	else
		writeq(0xffffffffffffffffULL,
		       (u8 *)oct->mmio[0].hw_addr + CN23XX_SLI_OUT_BP_EN_W1S);
}

static int cn23xx_setup_pf_device_regs(struct octeon_device *oct)
{
	cn23xx_enable_error_reporting(oct);

	/* program the MAC(0..3)_RINFO before setting up input/output regs */
	cn23xx_setup_global_mac_regs(oct);

	if (cn23xx_pf_setup_global_input_regs(oct))
		return -1;

	cn23xx_pf_setup_global_output_regs(oct);

	/* Default error timeout value should be 0x200000 to avoid host hang
	 * when reads invalid register
	 */
	octeon_write_csr64(oct, CN23XX_SLI_WINDOW_CTL,
			   CN23XX_SLI_WINDOW_CTL_DEFAULT);

	/* set SLI_PKT_IN_JABBER to handle large VXLAN packets */
	octeon_write_csr64(oct, CN23XX_SLI_PKT_IN_JABBER, CN23XX_INPUT_JABBER);
	return 0;
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

	if (oct->msix_on) {
		/* Set CINT_ENB to enable IQ interrupt   */
		writeq((pkt_in_done | CN23XX_INTR_CINT_ENB),
		       iq->inst_cnt_reg);
	} else {
		/* Clear the count by writing back what we read, but don't
		 * enable interrupts
		 */
		writeq(pkt_in_done, iq->inst_cnt_reg);
	}

	iq->reset_instr_cnt = 0;
}

static void cn23xx_setup_oq_regs(struct octeon_device *oct, u32 oq_no)
{
	u32 reg_val;
	struct octeon_droq *droq = oct->droq[oq_no];
	struct octeon_cn23xx_pf *cn23xx = (struct octeon_cn23xx_pf *)oct->chip;
	u64 time_threshold;
	u64 cnt_threshold;

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

	if (!oct->msix_on) {
		/* Enable this output queue to generate Packet Timer Interrupt
		 */
		reg_val =
		    octeon_read_csr(oct, CN23XX_SLI_OQ_PKT_CONTROL(oq_no));
		reg_val |= CN23XX_PKT_OUTPUT_CTL_TENB;
		octeon_write_csr(oct, CN23XX_SLI_OQ_PKT_CONTROL(oq_no),
				 reg_val);

		/* Enable this output queue to generate Packet Count Interrupt
		 */
		reg_val =
		    octeon_read_csr(oct, CN23XX_SLI_OQ_PKT_CONTROL(oq_no));
		reg_val |= CN23XX_PKT_OUTPUT_CTL_CENB;
		octeon_write_csr(oct, CN23XX_SLI_OQ_PKT_CONTROL(oq_no),
				 reg_val);
	} else {
		time_threshold = cn23xx_pf_get_oq_ticks(
		    oct, (u32)CFG_GET_OQ_INTR_TIME(cn23xx->conf));
		cnt_threshold = (u32)CFG_GET_OQ_INTR_PKT(cn23xx->conf);

		octeon_write_csr64(
		    oct, CN23XX_SLI_OQ_PKT_INT_LEVELS(oq_no),
		    ((time_threshold << 32 | cnt_threshold)));
	}
}

static int cn23xx_enable_io_queues(struct octeon_device *oct)
{
	u64 reg_val;
	u32 srn, ern, q_no;
	u32 loop = 1000;

	srn = oct->sriov_info.pf_srn;
	ern = srn + oct->num_iqs;

	for (q_no = srn; q_no < ern; q_no++) {
		/* set the corresponding IQ IS_64B bit */
		if (oct->io_qmask.iq64B & BIT_ULL(q_no - srn)) {
			reg_val = octeon_read_csr64(
			    oct, CN23XX_SLI_IQ_PKT_CONTROL64(q_no));
			reg_val = reg_val | CN23XX_PKT_INPUT_CTL_IS_64B;
			octeon_write_csr64(
			    oct, CN23XX_SLI_IQ_PKT_CONTROL64(q_no), reg_val);
		}

		/* set the corresponding IQ ENB bit */
		if (oct->io_qmask.iq & BIT_ULL(q_no - srn)) {
			/* IOQs are in reset by default in PEM2 mode,
			 * clearing reset bit
			 */
			reg_val = octeon_read_csr64(
			    oct, CN23XX_SLI_IQ_PKT_CONTROL64(q_no));

			if (reg_val & CN23XX_PKT_INPUT_CTL_RST) {
				while ((reg_val & CN23XX_PKT_INPUT_CTL_RST) &&
				       !(reg_val &
					 CN23XX_PKT_INPUT_CTL_QUIET) &&
				       --loop) {
					reg_val = octeon_read_csr64(
					    oct,
					    CN23XX_SLI_IQ_PKT_CONTROL64(q_no));
				}
				if (!loop) {
					dev_err(&oct->pci_dev->dev,
						"clearing the reset reg failed or setting the quiet reg failed for qno: %u\n",
						q_no);
					return -1;
				}
				reg_val = reg_val & ~CN23XX_PKT_INPUT_CTL_RST;
				octeon_write_csr64(
				    oct, CN23XX_SLI_IQ_PKT_CONTROL64(q_no),
				    reg_val);

				reg_val = octeon_read_csr64(
				    oct, CN23XX_SLI_IQ_PKT_CONTROL64(q_no));
				if (reg_val & CN23XX_PKT_INPUT_CTL_RST) {
					dev_err(&oct->pci_dev->dev,
						"clearing the reset failed for qno: %u\n",
						q_no);
					return -1;
				}
			}
			reg_val = octeon_read_csr64(
			    oct, CN23XX_SLI_IQ_PKT_CONTROL64(q_no));
			reg_val = reg_val | CN23XX_PKT_INPUT_CTL_RING_ENB;
			octeon_write_csr64(
			    oct, CN23XX_SLI_IQ_PKT_CONTROL64(q_no), reg_val);
		}
	}
	for (q_no = srn; q_no < ern; q_no++) {
		u32 reg_val;
		/* set the corresponding OQ ENB bit */
		if (oct->io_qmask.oq & BIT_ULL(q_no - srn)) {
			reg_val = octeon_read_csr(
			    oct, CN23XX_SLI_OQ_PKT_CONTROL(q_no));
			reg_val = reg_val | CN23XX_PKT_OUTPUT_CTL_RING_ENB;
			octeon_write_csr(oct, CN23XX_SLI_OQ_PKT_CONTROL(q_no),
					 reg_val);
		}
	}
	return 0;
}

static void cn23xx_disable_io_queues(struct octeon_device *oct)
{
	int q_no, loop;
	u64 d64;
	u32 d32;
	u32 srn, ern;

	srn = oct->sriov_info.pf_srn;
	ern = srn + oct->num_iqs;

	/*** Disable Input Queues. ***/
	for (q_no = srn; q_no < ern; q_no++) {
		loop = HZ;

		/* start the Reset for a particular ring */
		WRITE_ONCE(d64, octeon_read_csr64(
			   oct, CN23XX_SLI_IQ_PKT_CONTROL64(q_no)));
		WRITE_ONCE(d64, READ_ONCE(d64) &
					(~(CN23XX_PKT_INPUT_CTL_RING_ENB)));
		WRITE_ONCE(d64, READ_ONCE(d64) | CN23XX_PKT_INPUT_CTL_RST);
		octeon_write_csr64(oct, CN23XX_SLI_IQ_PKT_CONTROL64(q_no),
				   READ_ONCE(d64));

		/* Wait until hardware indicates that the particular IQ
		 * is out of reset.
		 */
		WRITE_ONCE(d64, octeon_read_csr64(
					oct, CN23XX_SLI_PKT_IOQ_RING_RST));
		while (!(READ_ONCE(d64) & BIT_ULL(q_no)) && loop--) {
			WRITE_ONCE(d64, octeon_read_csr64(
					oct, CN23XX_SLI_PKT_IOQ_RING_RST));
			schedule_timeout_uninterruptible(1);
		}

		/* Reset the doorbell register for this Input Queue. */
		octeon_write_csr(oct, CN23XX_SLI_IQ_DOORBELL(q_no), 0xFFFFFFFF);
		while (octeon_read_csr64(oct, CN23XX_SLI_IQ_DOORBELL(q_no)) &&
		       loop--) {
			schedule_timeout_uninterruptible(1);
		}
	}

	/*** Disable Output Queues. ***/
	for (q_no = srn; q_no < ern; q_no++) {
		loop = HZ;

		/* Wait until hardware indicates that the particular IQ
		 * is out of reset.It given that SLI_PKT_RING_RST is
		 * common for both IQs and OQs
		 */
		WRITE_ONCE(d64, octeon_read_csr64(
					oct, CN23XX_SLI_PKT_IOQ_RING_RST));
		while (!(READ_ONCE(d64) & BIT_ULL(q_no)) && loop--) {
			WRITE_ONCE(d64, octeon_read_csr64(
					oct, CN23XX_SLI_PKT_IOQ_RING_RST));
			schedule_timeout_uninterruptible(1);
		}

		/* Reset the doorbell register for this Output Queue. */
		octeon_write_csr(oct, CN23XX_SLI_OQ_PKTS_CREDIT(q_no),
				 0xFFFFFFFF);
		while (octeon_read_csr64(oct,
					 CN23XX_SLI_OQ_PKTS_CREDIT(q_no)) &&
		       loop--) {
			schedule_timeout_uninterruptible(1);
		}

		/* clear the SLI_PKT(0..63)_CNTS[CNT] reg value */
		WRITE_ONCE(d32, octeon_read_csr(
					oct, CN23XX_SLI_OQ_PKTS_SENT(q_no)));
		octeon_write_csr(oct, CN23XX_SLI_OQ_PKTS_SENT(q_no),
				 READ_ONCE(d32));
	}
}

static u64 cn23xx_pf_msix_interrupt_handler(void *dev)
{
	struct octeon_ioq_vector *ioq_vector = (struct octeon_ioq_vector *)dev;
	struct octeon_device *oct = ioq_vector->oct_dev;
	u64 pkts_sent;
	u64 ret = 0;
	struct octeon_droq *droq = oct->droq[ioq_vector->droq_index];

	dev_dbg(&oct->pci_dev->dev, "In %s octeon_dev @ %p\n", __func__, oct);

	if (!droq) {
		dev_err(&oct->pci_dev->dev, "23XX bringup FIXME: oct pfnum:%d ioq_vector->ioq_num :%d droq is NULL\n",
			oct->pf_num, ioq_vector->ioq_num);
		return 0;
	}

	pkts_sent = readq(droq->pkts_sent_reg);

	/* If our device has interrupted, then proceed. Also check
	 * for all f's if interrupt was triggered on an error
	 * and the PCI read fails.
	 */
	if (!pkts_sent || (pkts_sent == 0xFFFFFFFFFFFFFFFFULL))
		return ret;

	/* Write count reg in sli_pkt_cnts to clear these int.*/
	if ((pkts_sent & CN23XX_INTR_PO_INT) ||
	    (pkts_sent & CN23XX_INTR_PI_INT)) {
		if (pkts_sent & CN23XX_INTR_PO_INT)
			ret |= MSIX_PO_INT;
	}

	if (pkts_sent & CN23XX_INTR_PI_INT)
		/* We will clear the count when we update the read_index. */
		ret |= MSIX_PI_INT;

	/* Never need to handle msix mbox intr for pf. They arrive on the last
	 * msix
	 */
	return ret;
}

static irqreturn_t cn23xx_interrupt_handler(void *dev)
{
	struct octeon_device *oct = (struct octeon_device *)dev;
	struct octeon_cn23xx_pf *cn23xx = (struct octeon_cn23xx_pf *)oct->chip;
	u64 intr64;

	dev_dbg(&oct->pci_dev->dev, "In %s octeon_dev @ %p\n", __func__, oct);
	intr64 = readq(cn23xx->intr_sum_reg64);

	oct->int_status = 0;

	if (intr64 & CN23XX_INTR_ERR)
		dev_err(&oct->pci_dev->dev, "OCTEON[%d]: Error Intr: 0x%016llx\n",
			oct->octeon_id, CVM_CAST64(intr64));

	if (oct->msix_on != LIO_FLAG_MSIX_ENABLED) {
		if (intr64 & CN23XX_INTR_PKT_DATA)
			oct->int_status |= OCT_DEV_INTR_PKT_DATA;
	}

	if (intr64 & (CN23XX_INTR_DMA0_FORCE))
		oct->int_status |= OCT_DEV_INTR_DMA0_FORCE;
	if (intr64 & (CN23XX_INTR_DMA1_FORCE))
		oct->int_status |= OCT_DEV_INTR_DMA1_FORCE;

	/* Clear the current interrupts */
	writeq(intr64, cn23xx->intr_sum_reg64);

	return IRQ_HANDLED;
}

static void cn23xx_bar1_idx_setup(struct octeon_device *oct, u64 core_addr,
				  u32 idx, int valid)
{
	u64 bar1;
	u64 reg_adr;

	if (!valid) {
		reg_adr = lio_pci_readq(
			oct, CN23XX_PEM_BAR1_INDEX_REG(oct->pcie_port, idx));
		WRITE_ONCE(bar1, reg_adr);
		lio_pci_writeq(oct, (READ_ONCE(bar1) & 0xFFFFFFFEULL),
			       CN23XX_PEM_BAR1_INDEX_REG(oct->pcie_port, idx));
		reg_adr = lio_pci_readq(
			oct, CN23XX_PEM_BAR1_INDEX_REG(oct->pcie_port, idx));
		WRITE_ONCE(bar1, reg_adr);
		return;
	}

	/*  The PEM(0..3)_BAR1_INDEX(0..15)[ADDR_IDX]<23:4> stores
	 *  bits <41:22> of the Core Addr
	 */
	lio_pci_writeq(oct, (((core_addr >> 22) << 4) | PCI_BAR1_MASK),
		       CN23XX_PEM_BAR1_INDEX_REG(oct->pcie_port, idx));

	WRITE_ONCE(bar1, lio_pci_readq(
		   oct, CN23XX_PEM_BAR1_INDEX_REG(oct->pcie_port, idx)));
}

static void cn23xx_bar1_idx_write(struct octeon_device *oct, u32 idx, u32 mask)
{
	lio_pci_writeq(oct, mask,
		       CN23XX_PEM_BAR1_INDEX_REG(oct->pcie_port, idx));
}

static u32 cn23xx_bar1_idx_read(struct octeon_device *oct, u32 idx)
{
	return (u32)lio_pci_readq(
	    oct, CN23XX_PEM_BAR1_INDEX_REG(oct->pcie_port, idx));
}

/* always call with lock held */
static u32 cn23xx_update_read_index(struct octeon_instr_queue *iq)
{
	u32 new_idx;
	u32 last_done;
	u32 pkt_in_done = readl(iq->inst_cnt_reg);

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

static void cn23xx_enable_pf_interrupt(struct octeon_device *oct, u8 intr_flag)
{
	struct octeon_cn23xx_pf *cn23xx = (struct octeon_cn23xx_pf *)oct->chip;
	u64 intr_val = 0;

	/*  Divide the single write to multiple writes based on the flag. */
	/* Enable Interrupt */
	if (intr_flag == OCTEON_ALL_INTR) {
		writeq(cn23xx->intr_mask64, cn23xx->intr_enb_reg64);
	} else if (intr_flag & OCTEON_OUTPUT_INTR) {
		intr_val = readq(cn23xx->intr_enb_reg64);
		intr_val |= CN23XX_INTR_PKT_DATA;
		writeq(intr_val, cn23xx->intr_enb_reg64);
	}
}

static void cn23xx_disable_pf_interrupt(struct octeon_device *oct, u8 intr_flag)
{
	struct octeon_cn23xx_pf *cn23xx = (struct octeon_cn23xx_pf *)oct->chip;
	u64 intr_val = 0;

	/* Disable Interrupts */
	if (intr_flag == OCTEON_ALL_INTR) {
		writeq(0, cn23xx->intr_enb_reg64);
	} else if (intr_flag & OCTEON_OUTPUT_INTR) {
		intr_val = readq(cn23xx->intr_enb_reg64);
		intr_val &= ~CN23XX_INTR_PKT_DATA;
		writeq(intr_val, cn23xx->intr_enb_reg64);
	}
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
	if (!oct->msix_on)
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
	oct->fn_list.process_interrupt_regs = cn23xx_interrupt_handler;
	oct->fn_list.msix_interrupt_handler = cn23xx_pf_msix_interrupt_handler;

	oct->fn_list.soft_reset = cn23xx_pf_soft_reset;
	oct->fn_list.setup_device_regs = cn23xx_setup_pf_device_regs;
	oct->fn_list.update_iq_read_idx = cn23xx_update_read_index;

	oct->fn_list.bar1_idx_setup = cn23xx_bar1_idx_setup;
	oct->fn_list.bar1_idx_write = cn23xx_bar1_idx_write;
	oct->fn_list.bar1_idx_read = cn23xx_bar1_idx_read;

	oct->fn_list.enable_interrupt = cn23xx_enable_pf_interrupt;
	oct->fn_list.disable_interrupt = cn23xx_disable_pf_interrupt;

	oct->fn_list.enable_io_queues = cn23xx_enable_io_queues;
	oct->fn_list.disable_io_queues = cn23xx_disable_io_queues;

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

int cn23xx_fw_loaded(struct octeon_device *oct)
{
	u64 val;

	val = octeon_read_csr64(oct, CN23XX_SLI_SCRATCH1);
	return (val >> 1) & 1ULL;
}
