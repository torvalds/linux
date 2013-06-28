/*
 * Copyright (c) 2003-2012 Broadcom Corporation
 * All Rights Reserved
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * 1. Redistributions of source code must retain the above copyright
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the Broadcom
 * license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY BROADCOM ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL BROADCOM OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/resource.h>
#include <linux/phy.h>

#include <asm/netlogic/haldefs.h>
#include <asm/netlogic/common.h>
#include <asm/netlogic/xlr/fmn.h>
#include <asm/netlogic/xlr/xlr.h>
#include <asm/netlogic/psb-bootinfo.h>
#include <asm/netlogic/xlr/pic.h>
#include <asm/netlogic/xlr/iomap.h>

#include "platform_net.h"

/* Linux Net */
#define MAX_NUM_GMAC		8
#define MAX_NUM_XLS_GMAC	8
#define MAX_NUM_XLR_GMAC	4


static u32 xlr_gmac_offsets[] = {
	NETLOGIC_IO_GMAC_0_OFFSET, NETLOGIC_IO_GMAC_1_OFFSET,
	NETLOGIC_IO_GMAC_2_OFFSET, NETLOGIC_IO_GMAC_3_OFFSET,
	NETLOGIC_IO_GMAC_4_OFFSET, NETLOGIC_IO_GMAC_5_OFFSET,
	NETLOGIC_IO_GMAC_6_OFFSET, NETLOGIC_IO_GMAC_7_OFFSET
};

static u32 xlr_gmac_irqs[] = { PIC_GMAC_0_IRQ, PIC_GMAC_1_IRQ,
	PIC_GMAC_2_IRQ, PIC_GMAC_3_IRQ,
	PIC_GMAC_4_IRQ, PIC_GMAC_5_IRQ,
	PIC_GMAC_6_IRQ, PIC_GMAC_7_IRQ
};

static struct xlr_net_data ndata[MAX_NUM_GMAC];
static struct resource xlr_net_res[8][2];
static struct platform_device xlr_net_dev[8];
static u32 __iomem *gmac0_addr;
static u32 __iomem *gmac4_addr;
static u32 __iomem *gpio_addr;

static void config_mac(struct xlr_net_data *nd, int phy, u32 __iomem *serdes,
		u32 __iomem *pcs, int rfr, int tx, int *bkt_size,
		struct xlr_fmn_info *gmac_fmn_info, int phy_addr)
{
	nd->cpu_mask = nlm_current_node()->coremask;
	nd->phy_interface = phy;
	nd->rfr_station = rfr;
	nd->tx_stnid = tx;
	nd->mii_addr = gmac0_addr;
	nd->serdes_addr = serdes;
	nd->pcs_addr = pcs;
	nd->gpio_addr = gpio_addr;

	nd->bucket_size = bkt_size;
	nd->gmac_fmn_info = gmac_fmn_info;
	nd->phy_addr = phy_addr;
}

static void net_device_init(int id, struct resource *res, int offset, int irq)
{
	res[0].name = "gmac";
	res[0].start = CPHYSADDR(nlm_mmio_base(offset));
	res[0].end = res[0].start + 0xfff;
	res[0].flags = IORESOURCE_MEM;

	res[1].name = "gmac";
	res[1].start = irq;
	res[1].end = irq;
	res[1].flags = IORESOURCE_IRQ;

	xlr_net_dev[id].name = "xlr-net";
	xlr_net_dev[id].id = id;
	xlr_net_dev[id].num_resources = 2;
	xlr_net_dev[id].resource = res;
	xlr_net_dev[id].dev.platform_data = &ndata[id];
}

static void xls_gmac_init(void)
{
	int mac;

	gmac4_addr = ioremap(CPHYSADDR(
		nlm_mmio_base(NETLOGIC_IO_GMAC_4_OFFSET)), 0xfff);
	/* Passing GPIO base for serdes init. Only needed on sgmii ports*/
	gpio_addr = ioremap(CPHYSADDR(
		nlm_mmio_base(NETLOGIC_IO_GPIO_OFFSET)), 0xfff);

	switch (nlm_prom_info.board_major_version) {
	case 12:
		/* first block RGMII or XAUI, use RGMII */
		config_mac(&ndata[0],
			PHY_INTERFACE_MODE_RGMII,
			gmac0_addr,	/* serdes */
			gmac0_addr,	/* pcs */
			FMN_STNID_GMACRFR_0,
			FMN_STNID_GMAC0_TX0,
			xlr_board_fmn_config.bucket_size,
			&xlr_board_fmn_config.gmac[0],
			0);

		net_device_init(0, xlr_net_res[0], xlr_gmac_offsets[0],
				xlr_gmac_irqs[0]);
		platform_device_register(&xlr_net_dev[0]);

		/* second block is XAUI, not supported yet */
		break;
	default:
		/* default XLS config, all ports SGMII */
		for (mac = 0; mac < 4; mac++) {
			config_mac(&ndata[mac],
				PHY_INTERFACE_MODE_SGMII,
				gmac0_addr,	/* serdes */
				gmac0_addr,	/* pcs */
				FMN_STNID_GMACRFR_0,
				FMN_STNID_GMAC0_TX0 + mac,
				xlr_board_fmn_config.bucket_size,
				&xlr_board_fmn_config.gmac[0],
				/* PHY address according to chip/board */
				mac + 0x10);

			net_device_init(mac, xlr_net_res[mac],
					xlr_gmac_offsets[mac],
					xlr_gmac_irqs[mac]);
			platform_device_register(&xlr_net_dev[mac]);
		}

		for (mac = 4; mac < MAX_NUM_XLS_GMAC; mac++) {
			config_mac(&ndata[mac],
				PHY_INTERFACE_MODE_SGMII,
				gmac4_addr,	/* serdes */
				gmac4_addr,	/* pcs */
				FMN_STNID_GMAC1_FR_0,
				FMN_STNID_GMAC1_TX0 + mac - 4,
				xlr_board_fmn_config.bucket_size,
				&xlr_board_fmn_config.gmac[1],
				/* PHY address according to chip/board */
				mac + 0x10);

			net_device_init(mac, xlr_net_res[mac],
					xlr_gmac_offsets[mac],
					xlr_gmac_irqs[mac]);
			platform_device_register(&xlr_net_dev[mac]);
		}
	}
}

static void xlr_gmac_init(void)
{
	int mac;

	/* assume all GMACs for now */
	for (mac = 0; mac < MAX_NUM_XLR_GMAC; mac++) {
		config_mac(&ndata[mac],
			PHY_INTERFACE_MODE_RGMII,
			0,
			0,
			FMN_STNID_GMACRFR_0,
			FMN_STNID_GMAC0_TX0,
			xlr_board_fmn_config.bucket_size,
			&xlr_board_fmn_config.gmac[0],
			mac);

		net_device_init(mac, xlr_net_res[mac], xlr_gmac_offsets[mac],
				xlr_gmac_irqs[mac]);
		platform_device_register(&xlr_net_dev[mac]);
	}
}

static int __init xlr_net_init(void)
{
	gmac0_addr = ioremap(CPHYSADDR(
		nlm_mmio_base(NETLOGIC_IO_GMAC_0_OFFSET)), 0xfff);

	if (nlm_chip_is_xls())
		xls_gmac_init();
	else
		xlr_gmac_init();

	return 0;
}

arch_initcall(xlr_net_init);
