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
#include "cn68xx_regs.h"

static void lio_cn68xx_set_dpi_regs(struct octeon_device *oct)
{
	u32 i;
	u32 fifo_sizes[6] = { 3, 3, 1, 1, 1, 8 };

	lio_pci_writeq(oct, CN6XXX_DPI_DMA_CTL_MASK, CN6XXX_DPI_DMA_CONTROL);
	dev_dbg(&oct->pci_dev->dev, "DPI_DMA_CONTROL: 0x%016llx\n",
		lio_pci_readq(oct, CN6XXX_DPI_DMA_CONTROL));

	for (i = 0; i < 6; i++) {
		/* Prevent service of instruction queue for all DMA engines
		 * Engine 5 will remain 0. Engines 0 - 4 will be setup by
		 * core.
		 */
		lio_pci_writeq(oct, 0, CN6XXX_DPI_DMA_ENG_ENB(i));
		lio_pci_writeq(oct, fifo_sizes[i], CN6XXX_DPI_DMA_ENG_BUF(i));
		dev_dbg(&oct->pci_dev->dev, "DPI_ENG_BUF%d: 0x%016llx\n", i,
			lio_pci_readq(oct, CN6XXX_DPI_DMA_ENG_BUF(i)));
	}

	/* DPI_SLI_PRT_CFG has MPS and MRRS settings that will be set
	 * separately.
	 */

	lio_pci_writeq(oct, 1, CN6XXX_DPI_CTL);
	dev_dbg(&oct->pci_dev->dev, "DPI_CTL: 0x%016llx\n",
		lio_pci_readq(oct, CN6XXX_DPI_CTL));
}

static int lio_cn68xx_soft_reset(struct octeon_device *oct)
{
	lio_cn6xxx_soft_reset(oct);
	lio_cn68xx_set_dpi_regs(oct);

	return 0;
}

static void lio_cn68xx_setup_pkt_ctl_regs(struct octeon_device *oct)
{
	struct octeon_cn6xxx *cn68xx = (struct octeon_cn6xxx *)oct->chip;
	u64 pktctl, tx_pipe, max_oqs;

	pktctl = octeon_read_csr64(oct, CN6XXX_SLI_PKT_CTL);

	/* 68XX specific */
	max_oqs = CFG_GET_OQ_MAX_Q(CHIP_CONF(oct, cn6xxx));
	tx_pipe  = octeon_read_csr64(oct, CN68XX_SLI_TX_PIPE);
	tx_pipe &= 0xffffffffff00ffffULL; /* clear out NUMP field */
	tx_pipe |= max_oqs << 16; /* put max_oqs in NUMP field */
	octeon_write_csr64(oct, CN68XX_SLI_TX_PIPE, tx_pipe);

	if (CFG_GET_IS_SLI_BP_ON(cn68xx->conf))
		pktctl |= 0xF;
	else
		/* Disable per-port backpressure. */
		pktctl &= ~0xF;
	octeon_write_csr64(oct, CN6XXX_SLI_PKT_CTL, pktctl);
}

static int lio_cn68xx_setup_device_regs(struct octeon_device *oct)
{
	lio_cn6xxx_setup_pcie_mps(oct, PCIE_MPS_DEFAULT);
	lio_cn6xxx_setup_pcie_mrrs(oct, PCIE_MRRS_256B);
	lio_cn6xxx_enable_error_reporting(oct);

	lio_cn6xxx_setup_global_input_regs(oct);
	lio_cn68xx_setup_pkt_ctl_regs(oct);
	lio_cn6xxx_setup_global_output_regs(oct);

	/* Default error timeout value should be 0x200000 to avoid host hang
	 * when reads invalid register
	 */
	octeon_write_csr64(oct, CN6XXX_SLI_WINDOW_CTL, 0x200000ULL);

	return 0;
}

static inline void lio_cn68xx_vendor_message_fix(struct octeon_device *oct)
{
	u32 val = 0;

	/* Set M_VEND1_DRP and M_VEND0_DRP bits */
	pci_read_config_dword(oct->pci_dev, CN6XXX_PCIE_FLTMSK, &val);
	val |= 0x3;
	pci_write_config_dword(oct->pci_dev, CN6XXX_PCIE_FLTMSK, val);
}

static int lio_is_210nv(struct octeon_device *oct)
{
	u64 mio_qlm4_cfg = lio_pci_readq(oct, CN6XXX_MIO_QLM4_CFG);

	return ((mio_qlm4_cfg & CN6XXX_MIO_QLM_CFG_MASK) == 0);
}

int lio_setup_cn68xx_octeon_device(struct octeon_device *oct)
{
	struct octeon_cn6xxx *cn68xx = (struct octeon_cn6xxx *)oct->chip;
	u16 card_type = LIO_410NV;

	if (octeon_map_pci_barx(oct, 0, 0))
		return 1;

	if (octeon_map_pci_barx(oct, 1, MAX_BAR1_IOREMAP_SIZE)) {
		dev_err(&oct->pci_dev->dev, "%s CN68XX BAR1 map failed\n",
			__func__);
		octeon_unmap_pci_barx(oct, 0);
		return 1;
	}

	spin_lock_init(&cn68xx->lock_for_droq_int_enb_reg);

	oct->fn_list.setup_iq_regs = lio_cn6xxx_setup_iq_regs;
	oct->fn_list.setup_oq_regs = lio_cn6xxx_setup_oq_regs;

	oct->fn_list.process_interrupt_regs = lio_cn6xxx_process_interrupt_regs;
	oct->fn_list.soft_reset = lio_cn68xx_soft_reset;
	oct->fn_list.setup_device_regs = lio_cn68xx_setup_device_regs;
	oct->fn_list.update_iq_read_idx = lio_cn6xxx_update_read_index;

	oct->fn_list.bar1_idx_setup = lio_cn6xxx_bar1_idx_setup;
	oct->fn_list.bar1_idx_write = lio_cn6xxx_bar1_idx_write;
	oct->fn_list.bar1_idx_read = lio_cn6xxx_bar1_idx_read;

	oct->fn_list.enable_interrupt = lio_cn6xxx_enable_interrupt;
	oct->fn_list.disable_interrupt = lio_cn6xxx_disable_interrupt;

	oct->fn_list.enable_io_queues = lio_cn6xxx_enable_io_queues;
	oct->fn_list.disable_io_queues = lio_cn6xxx_disable_io_queues;

	lio_cn6xxx_setup_reg_address(oct, oct->chip, &oct->reg_list);

	/* Determine variant of card */
	if (lio_is_210nv(oct))
		card_type = LIO_210NV;

	cn68xx->conf = (struct octeon_config *)
		       oct_get_config_info(oct, card_type);
	if (!cn68xx->conf) {
		dev_err(&oct->pci_dev->dev, "%s No Config found for CN68XX %s\n",
			__func__,
			(card_type == LIO_410NV) ? LIO_410NV_NAME :
			LIO_210NV_NAME);
		octeon_unmap_pci_barx(oct, 0);
		octeon_unmap_pci_barx(oct, 1);
		return 1;
	}

	oct->coproc_clock_rate = 1000000ULL * lio_cn6xxx_coprocessor_clock(oct);

	lio_cn68xx_vendor_message_fix(oct);

	return 0;
}
