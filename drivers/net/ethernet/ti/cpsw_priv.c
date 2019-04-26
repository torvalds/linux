// SPDX-License-Identifier: GPL-2.0
/*
 * Texas Instruments Ethernet Switch Driver
 *
 * Copyright (C) 2019 Texas Instruments
 */

#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/skbuff.h>

#include "cpts.h"
#include "cpsw_ale.h"
#include "cpsw_priv.h"
#include "cpsw_sl.h"
#include "davinci_cpdma.h"

int cpsw_init_common(struct cpsw_common *cpsw, void __iomem *ss_regs,
		     int ale_ageout, phys_addr_t desc_mem_phys,
		     int descs_pool_size)
{
	u32 slave_offset, sliver_offset, slave_size;
	struct cpsw_ale_params ale_params;
	struct cpsw_platform_data *data;
	struct cpdma_params dma_params;
	struct device *dev = cpsw->dev;
	void __iomem *cpts_regs;
	int ret = 0, i;

	data = &cpsw->data;
	cpsw->rx_ch_num = 1;
	cpsw->tx_ch_num = 1;

	cpsw->version = readl(&cpsw->regs->id_ver);

	memset(&dma_params, 0, sizeof(dma_params));
	memset(&ale_params, 0, sizeof(ale_params));

	switch (cpsw->version) {
	case CPSW_VERSION_1:
		cpsw->host_port_regs = ss_regs + CPSW1_HOST_PORT_OFFSET;
		cpts_regs	     = ss_regs + CPSW1_CPTS_OFFSET;
		cpsw->hw_stats	     = ss_regs + CPSW1_HW_STATS;
		dma_params.dmaregs   = ss_regs + CPSW1_CPDMA_OFFSET;
		dma_params.txhdp     = ss_regs + CPSW1_STATERAM_OFFSET;
		ale_params.ale_regs  = ss_regs + CPSW1_ALE_OFFSET;
		slave_offset         = CPSW1_SLAVE_OFFSET;
		slave_size           = CPSW1_SLAVE_SIZE;
		sliver_offset        = CPSW1_SLIVER_OFFSET;
		dma_params.desc_mem_phys = 0;
		break;
	case CPSW_VERSION_2:
	case CPSW_VERSION_3:
	case CPSW_VERSION_4:
		cpsw->host_port_regs = ss_regs + CPSW2_HOST_PORT_OFFSET;
		cpts_regs	     = ss_regs + CPSW2_CPTS_OFFSET;
		cpsw->hw_stats	     = ss_regs + CPSW2_HW_STATS;
		dma_params.dmaregs   = ss_regs + CPSW2_CPDMA_OFFSET;
		dma_params.txhdp     = ss_regs + CPSW2_STATERAM_OFFSET;
		ale_params.ale_regs  = ss_regs + CPSW2_ALE_OFFSET;
		slave_offset         = CPSW2_SLAVE_OFFSET;
		slave_size           = CPSW2_SLAVE_SIZE;
		sliver_offset        = CPSW2_SLIVER_OFFSET;
		dma_params.desc_mem_phys = desc_mem_phys;
		break;
	default:
		dev_err(dev, "unknown version 0x%08x\n", cpsw->version);
		return -ENODEV;
	}

	for (i = 0; i < cpsw->data.slaves; i++) {
		struct cpsw_slave *slave = &cpsw->slaves[i];
		void __iomem		*regs = cpsw->regs;

		slave->slave_num = i;
		slave->data	= &cpsw->data.slave_data[i];
		slave->regs	= regs + slave_offset;
		slave->port_vlan = slave->data->dual_emac_res_vlan;
		slave->mac_sl = cpsw_sl_get("cpsw", dev, regs + sliver_offset);
		if (IS_ERR(slave->mac_sl))
			return PTR_ERR(slave->mac_sl);

		slave_offset  += slave_size;
		sliver_offset += SLIVER_SIZE;
	}

	ale_params.dev			= dev;
	ale_params.ale_ageout		= ale_ageout;
	ale_params.ale_entries		= data->ale_entries;
	ale_params.ale_ports		= CPSW_ALE_PORTS_NUM;

	cpsw->ale = cpsw_ale_create(&ale_params);
	if (!cpsw->ale) {
		dev_err(dev, "error initializing ale engine\n");
		return -ENODEV;
	}

	dma_params.dev		= dev;
	dma_params.rxthresh	= dma_params.dmaregs + CPDMA_RXTHRESH;
	dma_params.rxfree	= dma_params.dmaregs + CPDMA_RXFREE;
	dma_params.rxhdp	= dma_params.txhdp + CPDMA_RXHDP;
	dma_params.txcp		= dma_params.txhdp + CPDMA_TXCP;
	dma_params.rxcp		= dma_params.txhdp + CPDMA_RXCP;

	dma_params.num_chan		= data->channels;
	dma_params.has_soft_reset	= true;
	dma_params.min_packet_size	= CPSW_MIN_PACKET_SIZE;
	dma_params.desc_mem_size	= data->bd_ram_size;
	dma_params.desc_align		= 16;
	dma_params.has_ext_regs		= true;
	dma_params.desc_hw_addr         = dma_params.desc_mem_phys;
	dma_params.bus_freq_mhz		= cpsw->bus_freq_mhz;
	dma_params.descs_pool_size	= descs_pool_size;

	cpsw->dma = cpdma_ctlr_create(&dma_params);
	if (!cpsw->dma) {
		dev_err(dev, "error initializing dma\n");
		return -ENOMEM;
	}

	cpsw->cpts = cpts_create(cpsw->dev, cpts_regs, cpsw->dev->of_node);
	if (IS_ERR(cpsw->cpts)) {
		ret = PTR_ERR(cpsw->cpts);
		cpdma_ctlr_destroy(cpsw->dma);
	}

	return ret;
}
