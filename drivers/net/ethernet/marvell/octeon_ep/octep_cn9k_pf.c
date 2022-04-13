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

/* Reset all hardware Tx/Rx queues */
static void octep_reset_io_queues_cn93_pf(struct octep_device *oct)
{
}

/* Initialize windowed addresses to access some hardware registers */
static void octep_setup_pci_window_regs_cn93_pf(struct octep_device *oct)
{
}

/* Configure Hardware mapping: inform hardware which rings belong to PF. */
static void octep_configure_ring_mapping_cn93_pf(struct octep_device *oct)
{
}

/* Initialize configuration limits and initial active config 93xx PF. */
static void octep_init_config_cn93_pf(struct octep_device *oct)
{
	struct octep_config *conf = oct->conf;
	struct pci_dev *pdev = oct->pdev;
	u64 val;

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

	conf->ctrl_mbox_cfg.barmem_addr = (void __iomem *)oct->mmio[2].hw_addr + (0x400000ull * 7);
}

/* Setup registers for a hardware Tx Queue  */
static void octep_setup_iq_regs_cn93_pf(struct octep_device *oct, int iq_no)
{
}

/* Setup registers for a hardware Rx Queue  */
static void octep_setup_oq_regs_cn93_pf(struct octep_device *oct, int oq_no)
{
}

/* Setup registers for a PF mailbox */
static void octep_setup_mbox_regs_cn93_pf(struct octep_device *oct, int q_no)
{
}

/* Interrupts handler for all non-queue generic interrupts. */
static irqreturn_t octep_non_ioq_intr_handler_cn93_pf(void *dev)
{
	return IRQ_HANDLED;
}

/* Tx/Rx queue interrupt handler */
static irqreturn_t octep_ioq_intr_handler_cn93_pf(void *data)
{
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
}

/* Enable all interrupts */
static void octep_enable_interrupts_cn93_pf(struct octep_device *oct)
{
}

/* Disable all interrupts */
static void octep_disable_interrupts_cn93_pf(struct octep_device *oct)
{
}

/* Get new Octeon Read Index: index of descriptor that Octeon reads next. */
static u32 octep_update_iq_read_index_cn93_pf(struct octep_iq *iq)
{
	return 0;
}

/* Enable a hardware Tx Queue */
static void octep_enable_iq_cn93_pf(struct octep_device *oct, int iq_no)
{
}

/* Enable a hardware Rx Queue */
static void octep_enable_oq_cn93_pf(struct octep_device *oct, int oq_no)
{
}

/* Enable all hardware Tx/Rx Queues assined to PF */
static void octep_enable_io_queues_cn93_pf(struct octep_device *oct)
{
}

/* Disable a hardware Tx Queue assined to PF */
static void octep_disable_iq_cn93_pf(struct octep_device *oct, int iq_no)
{
}

/* Disable a hardware Rx Queue assined to PF */
static void octep_disable_oq_cn93_pf(struct octep_device *oct, int oq_no)
{
}

/* Disable all hardware Tx/Rx Queues assined to PF */
static void octep_disable_io_queues_cn93_pf(struct octep_device *oct)
{
}

/* Dump hardware registers (including Tx/Rx queues) for debugging. */
static void octep_dump_registers_cn93_pf(struct octep_device *oct)
{
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

	oct->hw_ops.non_ioq_intr_handler = octep_non_ioq_intr_handler_cn93_pf;
	oct->hw_ops.ioq_intr_handler = octep_ioq_intr_handler_cn93_pf;
	oct->hw_ops.soft_reset = octep_soft_reset_cn93_pf;
	oct->hw_ops.reinit_regs = octep_reinit_regs_cn93_pf;

	oct->hw_ops.enable_interrupts = octep_enable_interrupts_cn93_pf;
	oct->hw_ops.disable_interrupts = octep_disable_interrupts_cn93_pf;

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
