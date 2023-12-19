// SPDX-License-Identifier: GPL-2.0
/* Marvell Octeon EP (EndPoint) Ethernet Driver
 *
 * Copyright (C) 2020 Marvell.
 *
 */

#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>

#include "octep_config.h"
#include "octep_main.h"
#include "octep_regs_cn9k_pf.h"

#define CTRL_MBOX_MAX_PF	128
#define CTRL_MBOX_SZ		((size_t)(0x400000 / CTRL_MBOX_MAX_PF))

/* Names of Hardware non-queue generic interrupts */
static char *cn93_non_ioq_msix_names[] = {
	"epf_ire_rint",
	"epf_ore_rint",
	"epf_vfire_rint0",
	"epf_vfire_rint1",
	"epf_vfore_rint0",
	"epf_vfore_rint1",
	"epf_mbox_rint0",
	"epf_mbox_rint1",
	"epf_oei_rint",
	"epf_dma_rint",
	"epf_dma_vf_rint0",
	"epf_dma_vf_rint1",
	"epf_pp_vf_rint0",
	"epf_pp_vf_rint1",
	"epf_misc_rint",
	"epf_rsvd",
};

/* Dump useful hardware CSRs for debug purpose */
static void cn93_dump_regs(struct octep_device *oct, int qno)
{
	struct device *dev = &oct->pdev->dev;

	dev_info(dev, "IQ-%d register dump\n", qno);
	dev_info(dev, "R[%d]_IN_INSTR_DBELL[0x%llx]: 0x%016llx\n",
		 qno, CN93_SDP_R_IN_INSTR_DBELL(qno),
		 octep_read_csr64(oct, CN93_SDP_R_IN_INSTR_DBELL(qno)));
	dev_info(dev, "R[%d]_IN_CONTROL[0x%llx]: 0x%016llx\n",
		 qno, CN93_SDP_R_IN_CONTROL(qno),
		 octep_read_csr64(oct, CN93_SDP_R_IN_CONTROL(qno)));
	dev_info(dev, "R[%d]_IN_ENABLE[0x%llx]: 0x%016llx\n",
		 qno, CN93_SDP_R_IN_ENABLE(qno),
		 octep_read_csr64(oct, CN93_SDP_R_IN_ENABLE(qno)));
	dev_info(dev, "R[%d]_IN_INSTR_BADDR[0x%llx]: 0x%016llx\n",
		 qno, CN93_SDP_R_IN_INSTR_BADDR(qno),
		 octep_read_csr64(oct, CN93_SDP_R_IN_INSTR_BADDR(qno)));
	dev_info(dev, "R[%d]_IN_INSTR_RSIZE[0x%llx]: 0x%016llx\n",
		 qno, CN93_SDP_R_IN_INSTR_RSIZE(qno),
		 octep_read_csr64(oct, CN93_SDP_R_IN_INSTR_RSIZE(qno)));
	dev_info(dev, "R[%d]_IN_CNTS[0x%llx]: 0x%016llx\n",
		 qno, CN93_SDP_R_IN_CNTS(qno),
		 octep_read_csr64(oct, CN93_SDP_R_IN_CNTS(qno)));
	dev_info(dev, "R[%d]_IN_INT_LEVELS[0x%llx]: 0x%016llx\n",
		 qno, CN93_SDP_R_IN_INT_LEVELS(qno),
		 octep_read_csr64(oct, CN93_SDP_R_IN_INT_LEVELS(qno)));
	dev_info(dev, "R[%d]_IN_PKT_CNT[0x%llx]: 0x%016llx\n",
		 qno, CN93_SDP_R_IN_PKT_CNT(qno),
		 octep_read_csr64(oct, CN93_SDP_R_IN_PKT_CNT(qno)));
	dev_info(dev, "R[%d]_IN_BYTE_CNT[0x%llx]: 0x%016llx\n",
		 qno, CN93_SDP_R_IN_BYTE_CNT(qno),
		 octep_read_csr64(oct, CN93_SDP_R_IN_BYTE_CNT(qno)));

	dev_info(dev, "OQ-%d register dump\n", qno);
	dev_info(dev, "R[%d]_OUT_SLIST_DBELL[0x%llx]: 0x%016llx\n",
		 qno, CN93_SDP_R_OUT_SLIST_DBELL(qno),
		 octep_read_csr64(oct, CN93_SDP_R_OUT_SLIST_DBELL(qno)));
	dev_info(dev, "R[%d]_OUT_CONTROL[0x%llx]: 0x%016llx\n",
		 qno, CN93_SDP_R_OUT_CONTROL(qno),
		 octep_read_csr64(oct, CN93_SDP_R_OUT_CONTROL(qno)));
	dev_info(dev, "R[%d]_OUT_ENABLE[0x%llx]: 0x%016llx\n",
		 qno, CN93_SDP_R_OUT_ENABLE(qno),
		 octep_read_csr64(oct, CN93_SDP_R_OUT_ENABLE(qno)));
	dev_info(dev, "R[%d]_OUT_SLIST_BADDR[0x%llx]: 0x%016llx\n",
		 qno, CN93_SDP_R_OUT_SLIST_BADDR(qno),
		 octep_read_csr64(oct, CN93_SDP_R_OUT_SLIST_BADDR(qno)));
	dev_info(dev, "R[%d]_OUT_SLIST_RSIZE[0x%llx]: 0x%016llx\n",
		 qno, CN93_SDP_R_OUT_SLIST_RSIZE(qno),
		 octep_read_csr64(oct, CN93_SDP_R_OUT_SLIST_RSIZE(qno)));
	dev_info(dev, "R[%d]_OUT_CNTS[0x%llx]: 0x%016llx\n",
		 qno, CN93_SDP_R_OUT_CNTS(qno),
		 octep_read_csr64(oct, CN93_SDP_R_OUT_CNTS(qno)));
	dev_info(dev, "R[%d]_OUT_INT_LEVELS[0x%llx]: 0x%016llx\n",
		 qno, CN93_SDP_R_OUT_INT_LEVELS(qno),
		 octep_read_csr64(oct, CN93_SDP_R_OUT_INT_LEVELS(qno)));
	dev_info(dev, "R[%d]_OUT_PKT_CNT[0x%llx]: 0x%016llx\n",
		 qno, CN93_SDP_R_OUT_PKT_CNT(qno),
		 octep_read_csr64(oct, CN93_SDP_R_OUT_PKT_CNT(qno)));
	dev_info(dev, "R[%d]_OUT_BYTE_CNT[0x%llx]: 0x%016llx\n",
		 qno, CN93_SDP_R_OUT_BYTE_CNT(qno),
		 octep_read_csr64(oct, CN93_SDP_R_OUT_BYTE_CNT(qno)));
	dev_info(dev, "R[%d]_ERR_TYPE[0x%llx]: 0x%016llx\n",
		 qno, CN93_SDP_R_ERR_TYPE(qno),
		 octep_read_csr64(oct, CN93_SDP_R_ERR_TYPE(qno)));
}

/* Reset Hardware Tx queue */
static int cn93_reset_iq(struct octep_device *oct, int q_no)
{
	struct octep_config *conf = oct->conf;
	u64 val = 0ULL;

	dev_dbg(&oct->pdev->dev, "Reset PF IQ-%d\n", q_no);

	/* Get absolute queue number */
	q_no += conf->pf_ring_cfg.srn;

	/* Disable the Tx/Instruction Ring */
	octep_write_csr64(oct, CN93_SDP_R_IN_ENABLE(q_no), val);

	/* clear the Instruction Ring packet/byte counts and doorbell CSRs */
	octep_write_csr64(oct, CN93_SDP_R_IN_CNTS(q_no), val);
	octep_write_csr64(oct, CN93_SDP_R_IN_INT_LEVELS(q_no), val);
	octep_write_csr64(oct, CN93_SDP_R_IN_PKT_CNT(q_no), val);
	octep_write_csr64(oct, CN93_SDP_R_IN_BYTE_CNT(q_no), val);
	octep_write_csr64(oct, CN93_SDP_R_IN_INSTR_BADDR(q_no), val);
	octep_write_csr64(oct, CN93_SDP_R_IN_INSTR_RSIZE(q_no), val);

	val = 0xFFFFFFFF;
	octep_write_csr64(oct, CN93_SDP_R_IN_INSTR_DBELL(q_no), val);

	return 0;
}

/* Reset Hardware Rx queue */
static void cn93_reset_oq(struct octep_device *oct, int q_no)
{
	u64 val = 0ULL;

	q_no += CFG_GET_PORTS_PF_SRN(oct->conf);

	/* Disable Output (Rx) Ring */
	octep_write_csr64(oct, CN93_SDP_R_OUT_ENABLE(q_no), val);

	/* Clear count CSRs */
	val = octep_read_csr(oct, CN93_SDP_R_OUT_CNTS(q_no));
	octep_write_csr(oct, CN93_SDP_R_OUT_CNTS(q_no), val);

	octep_write_csr64(oct, CN93_SDP_R_OUT_PKT_CNT(q_no), 0xFFFFFFFFFULL);
	octep_write_csr64(oct, CN93_SDP_R_OUT_SLIST_DBELL(q_no), 0xFFFFFFFF);
}

/* Reset all hardware Tx/Rx queues */
static void octep_reset_io_queues_cn93_pf(struct octep_device *oct)
{
	struct pci_dev *pdev = oct->pdev;
	int q;

	dev_dbg(&pdev->dev, "Reset OCTEP_CN93 PF IO Queues\n");

	for (q = 0; q < CFG_GET_PORTS_ACTIVE_IO_RINGS(oct->conf); q++) {
		cn93_reset_iq(oct, q);
		cn93_reset_oq(oct, q);
	}
}

/* Initialize windowed addresses to access some hardware registers */
static void octep_setup_pci_window_regs_cn93_pf(struct octep_device *oct)
{
	u8 __iomem *bar0_pciaddr = oct->mmio[0].hw_addr;

	oct->pci_win_regs.pci_win_wr_addr = (u8 __iomem *)(bar0_pciaddr + CN93_SDP_WIN_WR_ADDR64);
	oct->pci_win_regs.pci_win_rd_addr = (u8 __iomem *)(bar0_pciaddr + CN93_SDP_WIN_RD_ADDR64);
	oct->pci_win_regs.pci_win_wr_data = (u8 __iomem *)(bar0_pciaddr + CN93_SDP_WIN_WR_DATA64);
	oct->pci_win_regs.pci_win_rd_data = (u8 __iomem *)(bar0_pciaddr + CN93_SDP_WIN_RD_DATA64);
}

/* Configure Hardware mapping: inform hardware which rings belong to PF. */
static void octep_configure_ring_mapping_cn93_pf(struct octep_device *oct)
{
	struct octep_config *conf = oct->conf;
	struct pci_dev *pdev = oct->pdev;
	u64 pf_srn = CFG_GET_PORTS_PF_SRN(oct->conf);
	int q;

	for (q = 0; q < CFG_GET_PORTS_ACTIVE_IO_RINGS(conf); q++) {
		u64 regval = 0;

		if (oct->pcie_port)
			regval = 8 << CN93_SDP_FUNC_SEL_EPF_BIT_POS;

		octep_write_csr64(oct, CN93_SDP_EPVF_RING(pf_srn + q), regval);

		regval = octep_read_csr64(oct, CN93_SDP_EPVF_RING(pf_srn + q));
		dev_dbg(&pdev->dev, "Write SDP_EPVF_RING[0x%llx] = 0x%llx\n",
			CN93_SDP_EPVF_RING(pf_srn + q), regval);
	}
}

/* Initialize configuration limits and initial active config 93xx PF. */
static void octep_init_config_cn93_pf(struct octep_device *oct)
{
	struct octep_config *conf = oct->conf;
	struct pci_dev *pdev = oct->pdev;
	u8 link = 0;
	u64 val;
	int pos;

	/* Read ring configuration:
	 * PF ring count, number of VFs and rings per VF supported
	 */
	val = octep_read_csr64(oct, CN93_SDP_EPF_RINFO);
	conf->sriov_cfg.max_rings_per_vf = CN93_SDP_EPF_RINFO_RPVF(val);
	conf->sriov_cfg.active_rings_per_vf = conf->sriov_cfg.max_rings_per_vf;
	conf->sriov_cfg.max_vfs = CN93_SDP_EPF_RINFO_NVFS(val);
	conf->sriov_cfg.active_vfs = conf->sriov_cfg.max_vfs;
	conf->sriov_cfg.vf_srn = CN93_SDP_EPF_RINFO_SRN(val);

	val = octep_read_csr64(oct, CN93_SDP_MAC_PF_RING_CTL(oct->pcie_port));
	conf->pf_ring_cfg.srn =  CN93_SDP_MAC_PF_RING_CTL_SRN(val);
	conf->pf_ring_cfg.max_io_rings = CN93_SDP_MAC_PF_RING_CTL_RPPF(val);
	conf->pf_ring_cfg.active_io_rings = conf->pf_ring_cfg.max_io_rings;
	dev_info(&pdev->dev, "pf_srn=%u rpvf=%u nvfs=%u rppf=%u\n",
		 conf->pf_ring_cfg.srn, conf->sriov_cfg.active_rings_per_vf,
		 conf->sriov_cfg.active_vfs, conf->pf_ring_cfg.active_io_rings);

	conf->iq.num_descs = OCTEP_IQ_MAX_DESCRIPTORS;
	conf->iq.instr_type = OCTEP_64BYTE_INSTR;
	conf->iq.pkind = 0;
	conf->iq.db_min = OCTEP_DB_MIN;
	conf->iq.intr_threshold = OCTEP_IQ_INTR_THRESHOLD;

	conf->oq.num_descs = OCTEP_OQ_MAX_DESCRIPTORS;
	conf->oq.buf_size = OCTEP_OQ_BUF_SIZE;
	conf->oq.refill_threshold = OCTEP_OQ_REFILL_THRESHOLD;
	conf->oq.oq_intr_pkt = OCTEP_OQ_INTR_PKT_THRESHOLD;
	conf->oq.oq_intr_time = OCTEP_OQ_INTR_TIME_THRESHOLD;

	conf->msix_cfg.non_ioq_msix = CN93_NUM_NON_IOQ_INTR;
	conf->msix_cfg.ioq_msix = conf->pf_ring_cfg.active_io_rings;
	conf->msix_cfg.non_ioq_msix_names = cn93_non_ioq_msix_names;

	pos = pci_find_ext_capability(oct->pdev, PCI_EXT_CAP_ID_SRIOV);
	if (pos) {
		pci_read_config_byte(oct->pdev,
				     pos + PCI_SRIOV_FUNC_LINK,
				     &link);
		link = PCI_DEVFN(PCI_SLOT(oct->pdev->devfn), link);
	}
	conf->ctrl_mbox_cfg.barmem_addr = (void __iomem *)oct->mmio[2].hw_addr +
					   CN93_PEM_BAR4_INDEX_OFFSET +
					   (link * CTRL_MBOX_SZ);

	conf->fw_info.hb_interval = OCTEP_DEFAULT_FW_HB_INTERVAL;
	conf->fw_info.hb_miss_count = OCTEP_DEFAULT_FW_HB_MISS_COUNT;
}

/* Setup registers for a hardware Tx Queue  */
static void octep_setup_iq_regs_cn93_pf(struct octep_device *oct, int iq_no)
{
	struct octep_iq *iq = oct->iq[iq_no];
	u32 reset_instr_cnt;
	u64 reg_val;

	iq_no += CFG_GET_PORTS_PF_SRN(oct->conf);
	reg_val = octep_read_csr64(oct, CN93_SDP_R_IN_CONTROL(iq_no));

	/* wait for IDLE to set to 1 */
	if (!(reg_val & CN93_R_IN_CTL_IDLE)) {
		do {
			reg_val = octep_read_csr64(oct, CN93_SDP_R_IN_CONTROL(iq_no));
		} while (!(reg_val & CN93_R_IN_CTL_IDLE));
	}

	reg_val |= CN93_R_IN_CTL_RDSIZE;
	reg_val |= CN93_R_IN_CTL_IS_64B;
	reg_val |= CN93_R_IN_CTL_ESR;
	octep_write_csr64(oct, CN93_SDP_R_IN_CONTROL(iq_no), reg_val);

	/* Write the start of the input queue's ring and its size  */
	octep_write_csr64(oct, CN93_SDP_R_IN_INSTR_BADDR(iq_no),
			  iq->desc_ring_dma);
	octep_write_csr64(oct, CN93_SDP_R_IN_INSTR_RSIZE(iq_no),
			  iq->max_count);

	/* Remember the doorbell & instruction count register addr
	 * for this queue
	 */
	iq->doorbell_reg = oct->mmio[0].hw_addr +
			   CN93_SDP_R_IN_INSTR_DBELL(iq_no);
	iq->inst_cnt_reg = oct->mmio[0].hw_addr +
			   CN93_SDP_R_IN_CNTS(iq_no);
	iq->intr_lvl_reg = oct->mmio[0].hw_addr +
			   CN93_SDP_R_IN_INT_LEVELS(iq_no);

	/* Store the current instruction counter (used in flush_iq calculation) */
	reset_instr_cnt = readl(iq->inst_cnt_reg);
	writel(reset_instr_cnt, iq->inst_cnt_reg);

	/* INTR_THRESHOLD is set to max(FFFFFFFF) to disable the INTR */
	reg_val = CFG_GET_IQ_INTR_THRESHOLD(oct->conf) & 0xffffffff;
	octep_write_csr64(oct, CN93_SDP_R_IN_INT_LEVELS(iq_no), reg_val);
}

/* Setup registers for a hardware Rx Queue  */
static void octep_setup_oq_regs_cn93_pf(struct octep_device *oct, int oq_no)
{
	u64 reg_val;
	u64 oq_ctl = 0ULL;
	u32 time_threshold = 0;
	struct octep_oq *oq = oct->oq[oq_no];

	oq_no += CFG_GET_PORTS_PF_SRN(oct->conf);
	reg_val = octep_read_csr64(oct, CN93_SDP_R_OUT_CONTROL(oq_no));

	/* wait for IDLE to set to 1 */
	if (!(reg_val & CN93_R_OUT_CTL_IDLE)) {
		do {
			reg_val = octep_read_csr64(oct, CN93_SDP_R_OUT_CONTROL(oq_no));
		} while (!(reg_val & CN93_R_OUT_CTL_IDLE));
	}

	reg_val &= ~(CN93_R_OUT_CTL_IMODE);
	reg_val &= ~(CN93_R_OUT_CTL_ROR_P);
	reg_val &= ~(CN93_R_OUT_CTL_NSR_P);
	reg_val &= ~(CN93_R_OUT_CTL_ROR_I);
	reg_val &= ~(CN93_R_OUT_CTL_NSR_I);
	reg_val &= ~(CN93_R_OUT_CTL_ES_I);
	reg_val &= ~(CN93_R_OUT_CTL_ROR_D);
	reg_val &= ~(CN93_R_OUT_CTL_NSR_D);
	reg_val &= ~(CN93_R_OUT_CTL_ES_D);
	reg_val |= (CN93_R_OUT_CTL_ES_P);

	octep_write_csr64(oct, CN93_SDP_R_OUT_CONTROL(oq_no), reg_val);
	octep_write_csr64(oct, CN93_SDP_R_OUT_SLIST_BADDR(oq_no),
			  oq->desc_ring_dma);
	octep_write_csr64(oct, CN93_SDP_R_OUT_SLIST_RSIZE(oq_no),
			  oq->max_count);

	oq_ctl = octep_read_csr64(oct, CN93_SDP_R_OUT_CONTROL(oq_no));
	oq_ctl &= ~0x7fffffULL;	//clear the ISIZE and BSIZE (22-0)
	oq_ctl |= (oq->buffer_size & 0xffff);	//populate the BSIZE (15-0)
	octep_write_csr64(oct, CN93_SDP_R_OUT_CONTROL(oq_no), oq_ctl);

	/* Get the mapped address of the pkt_sent and pkts_credit regs */
	oq->pkts_sent_reg = oct->mmio[0].hw_addr + CN93_SDP_R_OUT_CNTS(oq_no);
	oq->pkts_credit_reg = oct->mmio[0].hw_addr +
			      CN93_SDP_R_OUT_SLIST_DBELL(oq_no);

	time_threshold = CFG_GET_OQ_INTR_TIME(oct->conf);
	reg_val = ((u64)time_threshold << 32) |
		  CFG_GET_OQ_INTR_PKT(oct->conf);
	octep_write_csr64(oct, CN93_SDP_R_OUT_INT_LEVELS(oq_no), reg_val);
}

/* Setup registers for a PF mailbox */
static void octep_setup_mbox_regs_cn93_pf(struct octep_device *oct, int q_no)
{
	struct octep_mbox *mbox = oct->mbox[q_no];

	mbox->q_no = q_no;

	/* PF mbox interrupt reg */
	mbox->mbox_int_reg = oct->mmio[0].hw_addr + CN93_SDP_EPF_MBOX_RINT(0);

	/* PF to VF DATA reg. PF writes into this reg */
	mbox->mbox_write_reg = oct->mmio[0].hw_addr + CN93_SDP_R_MBOX_PF_VF_DATA(q_no);

	/* VF to PF DATA reg. PF reads from this reg */
	mbox->mbox_read_reg = oct->mmio[0].hw_addr + CN93_SDP_R_MBOX_VF_PF_DATA(q_no);
}

/* Poll OEI events like heartbeat */
static void octep_poll_oei_cn93_pf(struct octep_device *oct)
{
	u64 reg;

	reg = octep_read_csr64(oct, CN93_SDP_EPF_OEI_RINT);
	if (reg) {
		octep_write_csr64(oct, CN93_SDP_EPF_OEI_RINT, reg);
		if (reg & CN93_SDP_EPF_OEI_RINT_DATA_BIT_MBOX)
			queue_work(octep_wq, &oct->ctrl_mbox_task);
		else if (reg & CN93_SDP_EPF_OEI_RINT_DATA_BIT_HBEAT)
			atomic_set(&oct->hb_miss_cnt, 0);
	}
}

/* OEI interrupt handler */
static irqreturn_t octep_oei_intr_handler_cn93_pf(void *dev)
{
	struct octep_device *oct = (struct octep_device *)dev;

	octep_poll_oei_cn93_pf(oct);
	return IRQ_HANDLED;
}

/* Process non-ioq interrupts required to keep pf interface running.
 * OEI_RINT is needed for control mailbox
 */
static void octep_poll_non_ioq_interrupts_cn93_pf(struct octep_device *oct)
{
	octep_poll_oei_cn93_pf(oct);
}

/* Interrupt handler for input ring error interrupts. */
static irqreturn_t octep_ire_intr_handler_cn93_pf(void *dev)
{
	struct octep_device *oct = (struct octep_device *)dev;
	struct pci_dev *pdev = oct->pdev;
	u64 reg_val = 0;
	int i = 0;

	/* Check for IRERR INTR */
	reg_val = octep_read_csr64(oct, CN93_SDP_EPF_IRERR_RINT);
	if (reg_val) {
		dev_info(&pdev->dev,
			 "received IRERR_RINT intr: 0x%llx\n", reg_val);
		octep_write_csr64(oct, CN93_SDP_EPF_IRERR_RINT, reg_val);

		for (i = 0; i < CFG_GET_PORTS_ACTIVE_IO_RINGS(oct->conf); i++) {
			reg_val = octep_read_csr64(oct,
						   CN93_SDP_R_ERR_TYPE(i));
			if (reg_val) {
				dev_info(&pdev->dev,
					 "Received err type on IQ-%d: 0x%llx\n",
					 i, reg_val);
				octep_write_csr64(oct, CN93_SDP_R_ERR_TYPE(i),
						  reg_val);
			}
		}
	}
	return IRQ_HANDLED;
}

/* Interrupt handler for output ring error interrupts. */
static irqreturn_t octep_ore_intr_handler_cn93_pf(void *dev)
{
	struct octep_device *oct = (struct octep_device *)dev;
	struct pci_dev *pdev = oct->pdev;
	u64 reg_val = 0;
	int i = 0;

	/* Check for ORERR INTR */
	reg_val = octep_read_csr64(oct, CN93_SDP_EPF_ORERR_RINT);
	if (reg_val) {
		dev_info(&pdev->dev,
			 "Received ORERR_RINT intr: 0x%llx\n", reg_val);
		octep_write_csr64(oct, CN93_SDP_EPF_ORERR_RINT, reg_val);
		for (i = 0; i < CFG_GET_PORTS_ACTIVE_IO_RINGS(oct->conf); i++) {
			reg_val = octep_read_csr64(oct, CN93_SDP_R_ERR_TYPE(i));
			if (reg_val) {
				dev_info(&pdev->dev,
					 "Received err type on OQ-%d: 0x%llx\n",
					 i, reg_val);
				octep_write_csr64(oct, CN93_SDP_R_ERR_TYPE(i),
						  reg_val);
			}
		}
	}
	return IRQ_HANDLED;
}

/* Interrupt handler for vf input ring error interrupts. */
static irqreturn_t octep_vfire_intr_handler_cn93_pf(void *dev)
{
	struct octep_device *oct = (struct octep_device *)dev;
	struct pci_dev *pdev = oct->pdev;
	u64 reg_val = 0;

	/* Check for VFIRE INTR */
	reg_val = octep_read_csr64(oct, CN93_SDP_EPF_VFIRE_RINT(0));
	if (reg_val) {
		dev_info(&pdev->dev,
			 "Received VFIRE_RINT intr: 0x%llx\n", reg_val);
		octep_write_csr64(oct, CN93_SDP_EPF_VFIRE_RINT(0), reg_val);
	}
	return IRQ_HANDLED;
}

/* Interrupt handler for vf output ring error interrupts. */
static irqreturn_t octep_vfore_intr_handler_cn93_pf(void *dev)
{
	struct octep_device *oct = (struct octep_device *)dev;
	struct pci_dev *pdev = oct->pdev;
	u64 reg_val = 0;

	/* Check for VFORE INTR */
	reg_val = octep_read_csr64(oct, CN93_SDP_EPF_VFORE_RINT(0));
	if (reg_val) {
		dev_info(&pdev->dev,
			 "Received VFORE_RINT intr: 0x%llx\n", reg_val);
		octep_write_csr64(oct, CN93_SDP_EPF_VFORE_RINT(0), reg_val);
	}
	return IRQ_HANDLED;
}

/* Interrupt handler for dpi dma related interrupts. */
static irqreturn_t octep_dma_intr_handler_cn93_pf(void *dev)
{
	struct octep_device *oct = (struct octep_device *)dev;
	u64 reg_val = 0;

	/* Check for DMA INTR */
	reg_val = octep_read_csr64(oct, CN93_SDP_EPF_DMA_RINT);
	if (reg_val) {
		octep_write_csr64(oct, CN93_SDP_EPF_DMA_RINT, reg_val);
	}
	return IRQ_HANDLED;
}

/* Interrupt handler for dpi dma transaction error interrupts for VFs  */
static irqreturn_t octep_dma_vf_intr_handler_cn93_pf(void *dev)
{
	struct octep_device *oct = (struct octep_device *)dev;
	struct pci_dev *pdev = oct->pdev;
	u64 reg_val = 0;

	/* Check for DMA VF INTR */
	reg_val = octep_read_csr64(oct, CN93_SDP_EPF_DMA_VF_RINT(0));
	if (reg_val) {
		dev_info(&pdev->dev,
			 "Received DMA_VF_RINT intr: 0x%llx\n", reg_val);
		octep_write_csr64(oct, CN93_SDP_EPF_DMA_VF_RINT(0), reg_val);
	}
	return IRQ_HANDLED;
}

/* Interrupt handler for pp transaction error interrupts for VFs  */
static irqreturn_t octep_pp_vf_intr_handler_cn93_pf(void *dev)
{
	struct octep_device *oct = (struct octep_device *)dev;
	struct pci_dev *pdev = oct->pdev;
	u64 reg_val = 0;

	/* Check for PPVF INTR */
	reg_val = octep_read_csr64(oct, CN93_SDP_EPF_PP_VF_RINT(0));
	if (reg_val) {
		dev_info(&pdev->dev,
			 "Received PP_VF_RINT intr: 0x%llx\n", reg_val);
		octep_write_csr64(oct, CN93_SDP_EPF_PP_VF_RINT(0), reg_val);
	}
	return IRQ_HANDLED;
}

/* Interrupt handler for mac related interrupts. */
static irqreturn_t octep_misc_intr_handler_cn93_pf(void *dev)
{
	struct octep_device *oct = (struct octep_device *)dev;
	struct pci_dev *pdev = oct->pdev;
	u64 reg_val = 0;

	/* Check for MISC INTR */
	reg_val = octep_read_csr64(oct, CN93_SDP_EPF_MISC_RINT);
	if (reg_val) {
		dev_info(&pdev->dev,
			 "Received MISC_RINT intr: 0x%llx\n", reg_val);
		octep_write_csr64(oct, CN93_SDP_EPF_MISC_RINT, reg_val);
	}
	return IRQ_HANDLED;
}

/* Interrupts handler for all reserved interrupts. */
static irqreturn_t octep_rsvd_intr_handler_cn93_pf(void *dev)
{
	struct octep_device *oct = (struct octep_device *)dev;
	struct pci_dev *pdev = oct->pdev;

	dev_info(&pdev->dev, "Reserved interrupts raised; Ignore\n");
	return IRQ_HANDLED;
}

/* Tx/Rx queue interrupt handler */
static irqreturn_t octep_ioq_intr_handler_cn93_pf(void *data)
{
	struct octep_ioq_vector *vector = (struct octep_ioq_vector *)data;
	struct octep_oq *oq = vector->oq;

	napi_schedule_irqoff(oq->napi);
	return IRQ_HANDLED;
}

/* soft reset of 93xx */
static int octep_soft_reset_cn93_pf(struct octep_device *oct)
{
	dev_info(&oct->pdev->dev, "CN93XX: Doing soft reset\n");

	octep_write_csr64(oct, CN93_SDP_WIN_WR_MASK_REG, 0xFF);

	/* Set core domain reset bit */
	OCTEP_PCI_WIN_WRITE(oct, CN93_RST_CORE_DOMAIN_W1S, 1);
	/* Wait for 100ms as Octeon resets. */
	mdelay(100);
	/* clear core domain reset bit */
	OCTEP_PCI_WIN_WRITE(oct, CN93_RST_CORE_DOMAIN_W1C, 1);

	return 0;
}

/* Re-initialize Octeon hardware registers */
static void octep_reinit_regs_cn93_pf(struct octep_device *oct)
{
	u32 i;

	for (i = 0; i < CFG_GET_PORTS_ACTIVE_IO_RINGS(oct->conf); i++)
		oct->hw_ops.setup_iq_regs(oct, i);

	for (i = 0; i < CFG_GET_PORTS_ACTIVE_IO_RINGS(oct->conf); i++)
		oct->hw_ops.setup_oq_regs(oct, i);

	oct->hw_ops.enable_interrupts(oct);
	oct->hw_ops.enable_io_queues(oct);

	for (i = 0; i < CFG_GET_PORTS_ACTIVE_IO_RINGS(oct->conf); i++)
		writel(oct->oq[i]->max_count, oct->oq[i]->pkts_credit_reg);
}

/* Enable all interrupts */
static void octep_enable_interrupts_cn93_pf(struct octep_device *oct)
{
	u64 intr_mask = 0ULL;
	int srn, num_rings, i;

	srn = CFG_GET_PORTS_PF_SRN(oct->conf);
	num_rings = CFG_GET_PORTS_ACTIVE_IO_RINGS(oct->conf);

	for (i = 0; i < num_rings; i++)
		intr_mask |= (0x1ULL << (srn + i));

	octep_write_csr64(oct, CN93_SDP_EPF_IRERR_RINT_ENA_W1S, intr_mask);
	octep_write_csr64(oct, CN93_SDP_EPF_ORERR_RINT_ENA_W1S, intr_mask);
	octep_write_csr64(oct, CN93_SDP_EPF_OEI_RINT_ENA_W1S, -1ULL);

	octep_write_csr64(oct, CN93_SDP_EPF_VFIRE_RINT_ENA_W1S(0), -1ULL);
	octep_write_csr64(oct, CN93_SDP_EPF_VFORE_RINT_ENA_W1S(0), -1ULL);

	octep_write_csr64(oct, CN93_SDP_EPF_MISC_RINT_ENA_W1S, intr_mask);
	octep_write_csr64(oct, CN93_SDP_EPF_DMA_RINT_ENA_W1S, intr_mask);

	octep_write_csr64(oct, CN93_SDP_EPF_DMA_VF_RINT_ENA_W1S(0), -1ULL);
	octep_write_csr64(oct, CN93_SDP_EPF_PP_VF_RINT_ENA_W1S(0), -1ULL);
}

/* Disable all interrupts */
static void octep_disable_interrupts_cn93_pf(struct octep_device *oct)
{
	u64 intr_mask = 0ULL;
	int srn, num_rings, i;

	srn = CFG_GET_PORTS_PF_SRN(oct->conf);
	num_rings = CFG_GET_PORTS_ACTIVE_IO_RINGS(oct->conf);

	for (i = 0; i < num_rings; i++)
		intr_mask |= (0x1ULL << (srn + i));

	octep_write_csr64(oct, CN93_SDP_EPF_IRERR_RINT_ENA_W1C, intr_mask);
	octep_write_csr64(oct, CN93_SDP_EPF_ORERR_RINT_ENA_W1C, intr_mask);
	octep_write_csr64(oct, CN93_SDP_EPF_OEI_RINT_ENA_W1C, -1ULL);

	octep_write_csr64(oct, CN93_SDP_EPF_VFIRE_RINT_ENA_W1C(0), -1ULL);
	octep_write_csr64(oct, CN93_SDP_EPF_VFORE_RINT_ENA_W1C(0), -1ULL);

	octep_write_csr64(oct, CN93_SDP_EPF_MISC_RINT_ENA_W1C, intr_mask);
	octep_write_csr64(oct, CN93_SDP_EPF_DMA_RINT_ENA_W1C, intr_mask);

	octep_write_csr64(oct, CN93_SDP_EPF_DMA_VF_RINT_ENA_W1C(0), -1ULL);
	octep_write_csr64(oct, CN93_SDP_EPF_PP_VF_RINT_ENA_W1C(0), -1ULL);
}

/* Get new Octeon Read Index: index of descriptor that Octeon reads next. */
static u32 octep_update_iq_read_index_cn93_pf(struct octep_iq *iq)
{
	u32 pkt_in_done = readl(iq->inst_cnt_reg);
	u32 last_done, new_idx;

	last_done = pkt_in_done - iq->pkt_in_done;
	iq->pkt_in_done = pkt_in_done;

	new_idx = (iq->octep_read_index + last_done) % iq->max_count;

	return new_idx;
}

/* Enable a hardware Tx Queue */
static void octep_enable_iq_cn93_pf(struct octep_device *oct, int iq_no)
{
	u64 loop = HZ;
	u64 reg_val;

	iq_no += CFG_GET_PORTS_PF_SRN(oct->conf);

	octep_write_csr64(oct, CN93_SDP_R_IN_INSTR_DBELL(iq_no), 0xFFFFFFFF);

	while (octep_read_csr64(oct, CN93_SDP_R_IN_INSTR_DBELL(iq_no)) &&
	       loop--) {
		schedule_timeout_interruptible(1);
	}

	reg_val = octep_read_csr64(oct,  CN93_SDP_R_IN_INT_LEVELS(iq_no));
	reg_val |= (0x1ULL << 62);
	octep_write_csr64(oct, CN93_SDP_R_IN_INT_LEVELS(iq_no), reg_val);

	reg_val = octep_read_csr64(oct, CN93_SDP_R_IN_ENABLE(iq_no));
	reg_val |= 0x1ULL;
	octep_write_csr64(oct, CN93_SDP_R_IN_ENABLE(iq_no), reg_val);
}

/* Enable a hardware Rx Queue */
static void octep_enable_oq_cn93_pf(struct octep_device *oct, int oq_no)
{
	u64 reg_val = 0ULL;

	oq_no += CFG_GET_PORTS_PF_SRN(oct->conf);

	reg_val = octep_read_csr64(oct,  CN93_SDP_R_OUT_INT_LEVELS(oq_no));
	reg_val |= (0x1ULL << 62);
	octep_write_csr64(oct, CN93_SDP_R_OUT_INT_LEVELS(oq_no), reg_val);

	octep_write_csr64(oct, CN93_SDP_R_OUT_SLIST_DBELL(oq_no), 0xFFFFFFFF);

	reg_val = octep_read_csr64(oct, CN93_SDP_R_OUT_ENABLE(oq_no));
	reg_val |= 0x1ULL;
	octep_write_csr64(oct, CN93_SDP_R_OUT_ENABLE(oq_no), reg_val);
}

/* Enable all hardware Tx/Rx Queues assined to PF */
static void octep_enable_io_queues_cn93_pf(struct octep_device *oct)
{
	u8 q;

	for (q = 0; q < CFG_GET_PORTS_ACTIVE_IO_RINGS(oct->conf); q++) {
		octep_enable_iq_cn93_pf(oct, q);
		octep_enable_oq_cn93_pf(oct, q);
	}
}

/* Disable a hardware Tx Queue assined to PF */
static void octep_disable_iq_cn93_pf(struct octep_device *oct, int iq_no)
{
	u64 reg_val = 0ULL;

	iq_no += CFG_GET_PORTS_PF_SRN(oct->conf);

	reg_val = octep_read_csr64(oct, CN93_SDP_R_IN_ENABLE(iq_no));
	reg_val &= ~0x1ULL;
	octep_write_csr64(oct, CN93_SDP_R_IN_ENABLE(iq_no), reg_val);
}

/* Disable a hardware Rx Queue assined to PF */
static void octep_disable_oq_cn93_pf(struct octep_device *oct, int oq_no)
{
	u64 reg_val = 0ULL;

	oq_no += CFG_GET_PORTS_PF_SRN(oct->conf);
	reg_val = octep_read_csr64(oct, CN93_SDP_R_OUT_ENABLE(oq_no));
	reg_val &= ~0x1ULL;
	octep_write_csr64(oct, CN93_SDP_R_OUT_ENABLE(oq_no), reg_val);
}

/* Disable all hardware Tx/Rx Queues assined to PF */
static void octep_disable_io_queues_cn93_pf(struct octep_device *oct)
{
	int q = 0;

	for (q = 0; q < CFG_GET_PORTS_ACTIVE_IO_RINGS(oct->conf); q++) {
		octep_disable_iq_cn93_pf(oct, q);
		octep_disable_oq_cn93_pf(oct, q);
	}
}

/* Dump hardware registers (including Tx/Rx queues) for debugging. */
static void octep_dump_registers_cn93_pf(struct octep_device *oct)
{
	u8 srn, num_rings, q;

	srn = CFG_GET_PORTS_PF_SRN(oct->conf);
	num_rings = CFG_GET_PORTS_ACTIVE_IO_RINGS(oct->conf);

	for (q = srn; q < srn + num_rings; q++)
		cn93_dump_regs(oct, q);
}

/**
 * octep_device_setup_cn93_pf() - Setup Octeon device.
 *
 * @oct: Octeon device private data structure.
 *
 * - initialize hardware operations.
 * - get target side pcie port number for the device.
 * - setup window access to hardware registers.
 * - set initial configuration and max limits.
 * - setup hardware mapping of rings to the PF device.
 */
void octep_device_setup_cn93_pf(struct octep_device *oct)
{
	oct->hw_ops.setup_iq_regs = octep_setup_iq_regs_cn93_pf;
	oct->hw_ops.setup_oq_regs = octep_setup_oq_regs_cn93_pf;
	oct->hw_ops.setup_mbox_regs = octep_setup_mbox_regs_cn93_pf;

	oct->hw_ops.oei_intr_handler = octep_oei_intr_handler_cn93_pf;
	oct->hw_ops.ire_intr_handler = octep_ire_intr_handler_cn93_pf;
	oct->hw_ops.ore_intr_handler = octep_ore_intr_handler_cn93_pf;
	oct->hw_ops.vfire_intr_handler = octep_vfire_intr_handler_cn93_pf;
	oct->hw_ops.vfore_intr_handler = octep_vfore_intr_handler_cn93_pf;
	oct->hw_ops.dma_intr_handler = octep_dma_intr_handler_cn93_pf;
	oct->hw_ops.dma_vf_intr_handler = octep_dma_vf_intr_handler_cn93_pf;
	oct->hw_ops.pp_vf_intr_handler = octep_pp_vf_intr_handler_cn93_pf;
	oct->hw_ops.misc_intr_handler = octep_misc_intr_handler_cn93_pf;
	oct->hw_ops.rsvd_intr_handler = octep_rsvd_intr_handler_cn93_pf;
	oct->hw_ops.ioq_intr_handler = octep_ioq_intr_handler_cn93_pf;
	oct->hw_ops.soft_reset = octep_soft_reset_cn93_pf;
	oct->hw_ops.reinit_regs = octep_reinit_regs_cn93_pf;

	oct->hw_ops.enable_interrupts = octep_enable_interrupts_cn93_pf;
	oct->hw_ops.disable_interrupts = octep_disable_interrupts_cn93_pf;
	oct->hw_ops.poll_non_ioq_interrupts = octep_poll_non_ioq_interrupts_cn93_pf;

	oct->hw_ops.update_iq_read_idx = octep_update_iq_read_index_cn93_pf;

	oct->hw_ops.enable_iq = octep_enable_iq_cn93_pf;
	oct->hw_ops.enable_oq = octep_enable_oq_cn93_pf;
	oct->hw_ops.enable_io_queues = octep_enable_io_queues_cn93_pf;

	oct->hw_ops.disable_iq = octep_disable_iq_cn93_pf;
	oct->hw_ops.disable_oq = octep_disable_oq_cn93_pf;
	oct->hw_ops.disable_io_queues = octep_disable_io_queues_cn93_pf;
	oct->hw_ops.reset_io_queues = octep_reset_io_queues_cn93_pf;

	oct->hw_ops.dump_registers = octep_dump_registers_cn93_pf;

	octep_setup_pci_window_regs_cn93_pf(oct);

	oct->pcie_port = octep_read_csr64(oct, CN93_SDP_MAC_NUMBER) & 0xff;
	dev_info(&oct->pdev->dev,
		 "Octeon device using PCIE Port %d\n", oct->pcie_port);

	octep_init_config_cn93_pf(oct);
	octep_configure_ring_mapping_cn93_pf(oct);
}
