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

static struct resource xlr_net0_res[8];
static struct resource xlr_net1_res[8];
static u32 __iomem *gmac4_addr;
static u32 __iomem *gpio_addr;

static void xlr_resource_init(struct resource *res, int offset, int irq)
{
	res->name = "gmac";

	res->start = CPHYSADDR(nlm_mmio_base(offset));
	res->end = res->start + 0xfff;
	res->flags = IORESOURCE_MEM;

	res++;
	res->name = "gmac";
	res->start = res->end = irq;
	res->flags = IORESOURCE_IRQ;
}

static struct platform_device *gmac_controller2_init(void *gmac0_addr)
{
	int mac;
	static struct xlr_net_data ndata1 = {
		.phy_interface	= PHY_INTERFACE_MODE_SGMII,
		.rfr_station	= FMN_STNID_GMAC1_FR_0,
		.bucket_size	= xlr_board_fmn_config.bucket_size,
		.gmac_fmn_info	= &xlr_board_fmn_config.gmac[1],
	};

	static struct platform_device xlr_net_dev1 = {
		.name		= "xlr-net",
		.id		= 1,
		.dev.platform_data = &ndata1,
	};

	gmac4_addr = ioremap(CPHYSADDR(
		nlm_mmio_base(NETLOGIC_IO_GMAC_4_OFFSET)), 0xfff);
	ndata1.serdes_addr = gmac4_addr;
	ndata1.pcs_addr	= gmac4_addr;
	ndata1.mii_addr	= gmac0_addr;
	ndata1.gpio_addr = gpio_addr;
	ndata1.cpu_mask = nlm_current_node()->coremask;

	xlr_net_dev1.resource = xlr_net1_res;

	for (mac = 0; mac < 4; mac++) {
		ndata1.tx_stnid[mac] = FMN_STNID_GMAC1_TX0 + mac;
		ndata1.phy_addr[mac] = mac + 4 + 0x10;

		xlr_resource_init(&xlr_net1_res[mac * 2],
				xlr_gmac_offsets[mac + 4],
				xlr_gmac_irqs[mac + 4]);
	}
	xlr_net_dev1.num_resources = 8;

	return &xlr_net_dev1;
}

static void xls_gmac_init(void)
{
	int mac;
	struct platform_device *xlr_net_dev1;
	void __iomem *gmac0_addr = ioremap(CPHYSADDR(
		nlm_mmio_base(NETLOGIC_IO_GMAC_0_OFFSET)), 0xfff);

	static struct xlr_net_data ndata0 = {
		.rfr_station	= FMN_STNID_GMACRFR_0,
		.bucket_size	= xlr_board_fmn_config.bucket_size,
		.gmac_fmn_info	= &xlr_board_fmn_config.gmac[0],
	};

	static struct platform_device xlr_net_dev0 = {
		.name		= "xlr-net",
		.id		= 0,
	};
	xlr_net_dev0.dev.platform_data = &ndata0;
	ndata0.serdes_addr = gmac0_addr;
	ndata0.pcs_addr	= gmac0_addr;
	ndata0.mii_addr	= gmac0_addr;

	/* Passing GPIO base for serdes init. Only needed on sgmii ports */
	gpio_addr = ioremap(CPHYSADDR(
		nlm_mmio_base(NETLOGIC_IO_GPIO_OFFSET)), 0xfff);
	ndata0.gpio_addr = gpio_addr;
	ndata0.cpu_mask = nlm_current_node()->coremask;

	xlr_net_dev0.resource = xlr_net0_res;

	switch (nlm_prom_info.board_major_version) {
	case 12:
		/* first block RGMII or XAUI, use RGMII */
		ndata0.phy_interface = PHY_INTERFACE_MODE_RGMII;
		ndata0.tx_stnid[0] = FMN_STNID_GMAC0_TX0;
		ndata0.phy_addr[0] = 0;

		xlr_net_dev0.num_resources = 2;

		xlr_resource_init(&xlr_net0_res[0], xlr_gmac_offsets[0],
				xlr_gmac_irqs[0]);
		platform_device_register(&xlr_net_dev0);

		/* second block is XAUI, not supported yet */
		break;
	default:
		/* default XLS config, all ports SGMII */
		ndata0.phy_interface = PHY_INTERFACE_MODE_SGMII;
		for (mac = 0; mac < 4; mac++) {
			ndata0.tx_stnid[mac] = FMN_STNID_GMAC0_TX0 + mac;
			ndata0.phy_addr[mac] = mac + 0x10;

			xlr_resource_init(&xlr_net0_res[mac * 2],
					xlr_gmac_offsets[mac],
					xlr_gmac_irqs[mac]);
		}
		xlr_net_dev0.num_resources = 8;
		platform_device_register(&xlr_net_dev0);

		xlr_net_dev1 = gmac_controller2_init(gmac0_addr);
		platform_device_register(xlr_net_dev1);
	}
}

static void xlr_gmac_init(void)
{
	int mac;

	/* assume all GMACs for now */
	static struct xlr_net_data ndata0 = {
		.phy_interface	= PHY_INTERFACE_MODE_RGMII,
		.serdes_addr	= NULL,
		.pcs_addr	= NULL,
		.rfr_station	= FMN_STNID_GMACRFR_0,
		.bucket_size	= xlr_board_fmn_config.bucket_size,
		.gmac_fmn_info	= &xlr_board_fmn_config.gmac[0],
		.gpio_addr	= NULL,
	};


	static struct platform_device xlr_net_dev0 = {
		.name		= "xlr-net",
		.id		= 0,
		.dev.platform_data = &ndata0,
	};
	ndata0.mii_addr = ioremap(CPHYSADDR(
		nlm_mmio_base(NETLOGIC_IO_GMAC_0_OFFSET)), 0xfff);

	ndata0.cpu_mask = nlm_current_node()->coremask;

	for (mac = 0; mac < MAX_NUM_XLR_GMAC; mac++) {
		ndata0.tx_stnid[mac] = FMN_STNID_GMAC0_TX0 + mac;
		ndata0.phy_addr[mac] = mac;
		xlr_resource_init(&xlr_net0_res[mac * 2], xlr_gmac_offsets[mac],
				xlr_gmac_irqs[mac]);
	}
	xlr_net_dev0.num_resources = 8;
	xlr_net_dev0.resource = xlr_net0_res;

	platform_device_register(&xlr_net_dev0);
}

static int __init xlr_net_init(void)
{
	if (nlm_chip_is_xls())
		xls_gmac_init();
	else
		xlr_gmac_init();

	return 0;
}

arch_initcall(xlr_net_init);
