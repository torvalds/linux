// SPDX-License-Identifier: GPL-2.0
/* Marvell Octeon EP (EndPoint) VF Ethernet Driver
 *
 * Copyright (C) 2020 Marvell.
 *
 */

#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>

#include "octep_vf_config.h"
#include "octep_vf_main.h"
#include "octep_vf_regs_cn9k.h"

/* Reset all hardware Tx/Rx queues */
static void octep_vf_reset_io_queues_cn93(struct octep_vf_device *oct)
{
}

/* Initialize configuration limits and initial active config */
static void octep_vf_init_config_cn93_vf(struct octep_vf_device *oct)
{
	struct octep_vf_config *conf = oct->conf;
	u64 reg_val;

	reg_val = octep_vf_read_csr64(oct, CN93_VF_SDP_R_IN_CONTROL(0));
	conf->ring_cfg.max_io_rings = (reg_val >> CN93_VF_R_IN_CTL_RPVF_POS) &
				      CN93_VF_R_IN_CTL_RPVF_MASK;
	conf->ring_cfg.active_io_rings = conf->ring_cfg.max_io_rings;

	conf->iq.num_descs = OCTEP_VF_IQ_MAX_DESCRIPTORS;
	conf->iq.instr_type = OCTEP_VF_64BYTE_INSTR;
	conf->iq.db_min = OCTEP_VF_DB_MIN;
	conf->iq.intr_threshold = OCTEP_VF_IQ_INTR_THRESHOLD;

	conf->oq.num_descs = OCTEP_VF_OQ_MAX_DESCRIPTORS;
	conf->oq.buf_size = OCTEP_VF_OQ_BUF_SIZE;
	conf->oq.refill_threshold = OCTEP_VF_OQ_REFILL_THRESHOLD;
	conf->oq.oq_intr_pkt = OCTEP_VF_OQ_INTR_PKT_THRESHOLD;
	conf->oq.oq_intr_time = OCTEP_VF_OQ_INTR_TIME_THRESHOLD;

	conf->msix_cfg.ioq_msix = conf->ring_cfg.active_io_rings;
}

/* Setup registers for a hardware Tx Queue  */
static void octep_vf_setup_iq_regs_cn93(struct octep_vf_device *oct, int iq_no)
{
}

/* Setup registers for a hardware Rx Queue  */
static void octep_vf_setup_oq_regs_cn93(struct octep_vf_device *oct, int oq_no)
{
}

/* Setup registers for a VF mailbox */
static void octep_vf_setup_mbox_regs_cn93(struct octep_vf_device *oct, int q_no)
{
}

/* Tx/Rx queue interrupt handler */
static irqreturn_t octep_vf_ioq_intr_handler_cn93(void *data)
{
	return IRQ_HANDLED;
}

/* Re-initialize Octeon hardware registers */
static void octep_vf_reinit_regs_cn93(struct octep_vf_device *oct)
{
}

/* Enable all interrupts */
static void octep_vf_enable_interrupts_cn93(struct octep_vf_device *oct)
{
}

/* Disable all interrupts */
static void octep_vf_disable_interrupts_cn93(struct octep_vf_device *oct)
{
}

/* Get new Octeon Read Index: index of descriptor that Octeon reads next. */
static u32 octep_vf_update_iq_read_index_cn93(struct octep_vf_iq *iq)
{
	return 0;
}

/* Enable a hardware Tx Queue */
static void octep_vf_enable_iq_cn93(struct octep_vf_device *oct, int iq_no)
{
}

/* Enable a hardware Rx Queue */
static void octep_vf_enable_oq_cn93(struct octep_vf_device *oct, int oq_no)
{
}

/* Enable all hardware Tx/Rx Queues assigned to VF */
static void octep_vf_enable_io_queues_cn93(struct octep_vf_device *oct)
{
}

/* Disable a hardware Tx Queue assigned to VF */
static void octep_vf_disable_iq_cn93(struct octep_vf_device *oct, int iq_no)
{
}

/* Disable a hardware Rx Queue assigned to VF */
static void octep_vf_disable_oq_cn93(struct octep_vf_device *oct, int oq_no)
{
}

/* Disable all hardware Tx/Rx Queues assigned to VF */
static void octep_vf_disable_io_queues_cn93(struct octep_vf_device *oct)
{
}

/* Dump hardware registers (including Tx/Rx queues) for debugging. */
static void octep_vf_dump_registers_cn93(struct octep_vf_device *oct)
{
}

/**
 * octep_vf_device_setup_cn93() - Setup Octeon device.
 *
 * @oct: Octeon device private data structure.
 *
 * - initialize hardware operations.
 * - get target side pcie port number for the device.
 * - set initial configuration and max limits.
 */
void octep_vf_device_setup_cn93(struct octep_vf_device *oct)
{
	oct->hw_ops.setup_iq_regs = octep_vf_setup_iq_regs_cn93;
	oct->hw_ops.setup_oq_regs = octep_vf_setup_oq_regs_cn93;
	oct->hw_ops.setup_mbox_regs = octep_vf_setup_mbox_regs_cn93;

	oct->hw_ops.ioq_intr_handler = octep_vf_ioq_intr_handler_cn93;
	oct->hw_ops.reinit_regs = octep_vf_reinit_regs_cn93;

	oct->hw_ops.enable_interrupts = octep_vf_enable_interrupts_cn93;
	oct->hw_ops.disable_interrupts = octep_vf_disable_interrupts_cn93;

	oct->hw_ops.update_iq_read_idx = octep_vf_update_iq_read_index_cn93;

	oct->hw_ops.enable_iq = octep_vf_enable_iq_cn93;
	oct->hw_ops.enable_oq = octep_vf_enable_oq_cn93;
	oct->hw_ops.enable_io_queues = octep_vf_enable_io_queues_cn93;

	oct->hw_ops.disable_iq = octep_vf_disable_iq_cn93;
	oct->hw_ops.disable_oq = octep_vf_disable_oq_cn93;
	oct->hw_ops.disable_io_queues = octep_vf_disable_io_queues_cn93;
	oct->hw_ops.reset_io_queues = octep_vf_reset_io_queues_cn93;

	oct->hw_ops.dump_registers = octep_vf_dump_registers_cn93;
	octep_vf_init_config_cn93_vf(oct);
}
