/*
 * arch/arm/mach-spear13xx/spear13xx.c
 *
 * SPEAr13XX machines common source file
 *
 * Copyright (C) 2012 ST Microelectronics
 * Viresh Kumar <viresh.linux@gmail.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#define pr_fmt(fmt) "SPEAr13xx: " fmt

#include <linux/amba/pl022.h>
#include <linux/clk.h>
#include <linux/dw_dmac.h>
#include <linux/err.h>
#include <linux/of.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/mach/map.h>
#include <asm/smp_twd.h>
#include <mach/dma.h>
#include <mach/generic.h>
#include <mach/spear.h>

/* common dw_dma filter routine to be used by peripherals */
bool dw_dma_filter(struct dma_chan *chan, void *slave)
{
	struct dw_dma_slave *dws = (struct dw_dma_slave *)slave;

	if (chan->device->dev == dws->dma_dev) {
		chan->private = slave;
		return true;
	} else {
		return false;
	}
}

/* ssp device registration */
static struct dw_dma_slave ssp_dma_param[] = {
	{
		/* Tx */
		.cfg_hi = DWC_CFGH_DST_PER(DMA_REQ_SSP0_TX),
		.cfg_lo = 0,
		.src_master = DMA_MASTER_MEMORY,
		.dst_master = DMA_MASTER_SSP0,
	}, {
		/* Rx */
		.cfg_hi = DWC_CFGH_SRC_PER(DMA_REQ_SSP0_RX),
		.cfg_lo = 0,
		.src_master = DMA_MASTER_SSP0,
		.dst_master = DMA_MASTER_MEMORY,
	}
};

struct pl022_ssp_controller pl022_plat_data = {
	.enable_dma = 1,
	.dma_filter = dw_dma_filter,
	.dma_rx_param = &ssp_dma_param[1],
	.dma_tx_param = &ssp_dma_param[0],
};

/* CF device registration */
struct dw_dma_slave cf_dma_priv = {
	.cfg_hi = 0,
	.cfg_lo = 0,
	.src_master = 0,
	.dst_master = 0,
};

/* dmac device registeration */
struct dw_dma_platform_data dmac_plat_data = {
	.nr_channels = 8,
	.chan_allocation_order = CHAN_ALLOCATION_DESCENDING,
	.chan_priority = CHAN_PRIORITY_DESCENDING,
	.block_size = 4095U,
	.nr_masters = 2,
	.data_width = { 3, 3, 0, 0 },
};

void __init spear13xx_l2x0_init(void)
{
	/*
	 * 512KB (64KB/way), 8-way associativity, parity supported
	 *
	 * FIXME: 9th bit, of Auxillary Controller register must be set
	 * for some spear13xx devices for stable L2 operation.
	 *
	 * Enable Early BRESP, L2 prefetch for Instruction and Data,
	 * write alloc and 'Full line of zero' options
	 *
	 */

	writel_relaxed(0x06, VA_L2CC_BASE + L2X0_PREFETCH_CTRL);

	/*
	 * Program following latencies in order to make
	 * SPEAr1340 work at 600 MHz
	 */
	writel_relaxed(0x221, VA_L2CC_BASE + L2X0_TAG_LATENCY_CTRL);
	writel_relaxed(0x441, VA_L2CC_BASE + L2X0_DATA_LATENCY_CTRL);
	l2x0_init(VA_L2CC_BASE, 0x70A60001, 0xfe00ffff);
}

/*
 * Following will create 16MB static virtual/physical mappings
 * PHYSICAL		VIRTUAL
 * 0xB3000000		0xFE000000
 * 0xE0000000		0xFD000000
 * 0xEC000000		0xFC000000
 * 0xED000000		0xFB000000
 */
struct map_desc spear13xx_io_desc[] __initdata = {
	{
		.virtual	= (unsigned long)VA_PERIP_GRP2_BASE,
		.pfn		= __phys_to_pfn(PERIP_GRP2_BASE),
		.length		= SZ_16M,
		.type		= MT_DEVICE
	}, {
		.virtual	= (unsigned long)VA_PERIP_GRP1_BASE,
		.pfn		= __phys_to_pfn(PERIP_GRP1_BASE),
		.length		= SZ_16M,
		.type		= MT_DEVICE
	}, {
		.virtual	= (unsigned long)VA_A9SM_AND_MPMC_BASE,
		.pfn		= __phys_to_pfn(A9SM_AND_MPMC_BASE),
		.length		= SZ_16M,
		.type		= MT_DEVICE
	}, {
		.virtual	= (unsigned long)VA_L2CC_BASE,
		.pfn		= __phys_to_pfn(L2CC_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	},
};

/* This will create static memory mapping for selected devices */
void __init spear13xx_map_io(void)
{
	iotable_init(spear13xx_io_desc, ARRAY_SIZE(spear13xx_io_desc));
}

static void __init spear13xx_clk_init(void)
{
	if (of_machine_is_compatible("st,spear1310"))
		spear1310_clk_init();
	else if (of_machine_is_compatible("st,spear1340"))
		spear1340_clk_init();
	else
		pr_err("%s: Unknown machine\n", __func__);
}

static void __init spear13xx_timer_init(void)
{
	char pclk_name[] = "osc_24m_clk";
	struct clk *gpt_clk, *pclk;

	spear13xx_clk_init();

	/* get the system timer clock */
	gpt_clk = clk_get_sys("gpt0", NULL);
	if (IS_ERR(gpt_clk)) {
		pr_err("%s:couldn't get clk for gpt\n", __func__);
		BUG();
	}

	/* get the suitable parent clock for timer*/
	pclk = clk_get(NULL, pclk_name);
	if (IS_ERR(pclk)) {
		pr_err("%s:couldn't get %s as parent for gpt\n", __func__,
				pclk_name);
		BUG();
	}

	clk_set_parent(gpt_clk, pclk);
	clk_put(gpt_clk);
	clk_put(pclk);

	spear_setup_of_timer();
	twd_local_timer_of_register();
}

struct sys_timer spear13xx_timer = {
	.init = spear13xx_timer_init,
};
