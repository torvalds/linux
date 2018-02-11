// SPDX-License-Identifier: GPL-2.0
/* Wrapper for DMA channel allocator that starts clocks etc */

#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <mach/dma.h>
#include <hwregs/reg_map.h>
#include <hwregs/reg_rdwr.h>
#include <hwregs/marb_defs.h>
#include <hwregs/clkgen_defs.h>
#include <hwregs/strmux_defs.h>
#include <linux/errno.h>
#include <arbiter.h>

static char used_dma_channels[MAX_DMA_CHANNELS];
static const char *used_dma_channels_users[MAX_DMA_CHANNELS];

static DEFINE_SPINLOCK(dma_lock);

int crisv32_request_dma(unsigned int dmanr, const char *device_id,
	unsigned options, unsigned int bandwidth, enum dma_owner owner)
{
	unsigned long flags;
	reg_clkgen_rw_clk_ctrl clk_ctrl;
	reg_strmux_rw_cfg strmux_cfg;

	if (crisv32_arbiter_allocate_bandwidth(dmanr,
			options & DMA_INT_MEM ? INT_REGION : EXT_REGION,
			bandwidth))
		return -ENOMEM;

	spin_lock_irqsave(&dma_lock, flags);

	if (used_dma_channels[dmanr]) {
		spin_unlock_irqrestore(&dma_lock, flags);
		if (options & DMA_VERBOSE_ON_ERROR)
			printk(KERN_ERR "Failed to request DMA %i for %s, "
				"already allocated by %s\n",
				dmanr,
				device_id,
				used_dma_channels_users[dmanr]);

		if (options & DMA_PANIC_ON_ERROR)
			panic("request_dma error!");
		return -EBUSY;
	}
	clk_ctrl = REG_RD(clkgen, regi_clkgen, rw_clk_ctrl);
	strmux_cfg = REG_RD(strmux, regi_strmux, rw_cfg);

	switch (dmanr) {
	case 0:
	case 1:
		clk_ctrl.dma0_1_eth = 1;
		break;
	case 2:
	case 3:
		clk_ctrl.dma2_3_strcop = 1;
		break;
	case 4:
	case 5:
		clk_ctrl.dma4_5_iop = 1;
		break;
	case 6:
	case 7:
		clk_ctrl.sser_ser_dma6_7 = 1;
		break;
	case 9:
	case 11:
		clk_ctrl.dma9_11 = 1;
		break;
#if MAX_DMA_CHANNELS-1 != 11
#error Check dma.c
#endif
	default:
		spin_unlock_irqrestore(&dma_lock, flags);
		if (options & DMA_VERBOSE_ON_ERROR)
			printk(KERN_ERR "Failed to request DMA %i for %s, "
				"only 0-%i valid)\n",
				dmanr, device_id, MAX_DMA_CHANNELS-1);

		if (options & DMA_PANIC_ON_ERROR)
			panic("request_dma error!");
		return -EINVAL;
	}

	switch (owner) {
	case dma_eth:
		if (dmanr == 0)
			strmux_cfg.dma0 = regk_strmux_eth;
		else if (dmanr == 1)
			strmux_cfg.dma1 = regk_strmux_eth;
		else
			panic("Invalid DMA channel for eth\n");
		break;
	case dma_ser0:
		if (dmanr == 0)
			strmux_cfg.dma0 = regk_strmux_ser0;
		else if (dmanr == 1)
			strmux_cfg.dma1 = regk_strmux_ser0;
		else
			panic("Invalid DMA channel for ser0\n");
		break;
	case dma_ser3:
		if (dmanr == 2)
			strmux_cfg.dma2 = regk_strmux_ser3;
		else if (dmanr == 3)
			strmux_cfg.dma3 = regk_strmux_ser3;
		else
			panic("Invalid DMA channel for ser3\n");
		break;
	case dma_strp:
		if (dmanr == 2)
			strmux_cfg.dma2 = regk_strmux_strcop;
		else if (dmanr == 3)
			strmux_cfg.dma3 = regk_strmux_strcop;
		else
			panic("Invalid DMA channel for strp\n");
		break;
	case dma_ser1:
		if (dmanr == 4)
			strmux_cfg.dma4 = regk_strmux_ser1;
		else if (dmanr == 5)
			strmux_cfg.dma5 = regk_strmux_ser1;
		else
			panic("Invalid DMA channel for ser1\n");
		break;
	case dma_iop:
		if (dmanr == 4)
			strmux_cfg.dma4 = regk_strmux_iop;
		else if (dmanr == 5)
			strmux_cfg.dma5 = regk_strmux_iop;
		else
			panic("Invalid DMA channel for iop\n");
		break;
	case dma_ser2:
		if (dmanr == 6)
			strmux_cfg.dma6 = regk_strmux_ser2;
		else if (dmanr == 7)
			strmux_cfg.dma7 = regk_strmux_ser2;
		else
			panic("Invalid DMA channel for ser2\n");
		break;
	case dma_sser:
		if (dmanr == 6)
			strmux_cfg.dma6 = regk_strmux_sser;
		else if (dmanr == 7)
			strmux_cfg.dma7 = regk_strmux_sser;
		else
			panic("Invalid DMA channel for sser\n");
		break;
	case dma_ser4:
		if (dmanr == 9)
			strmux_cfg.dma9 = regk_strmux_ser4;
		else
			panic("Invalid DMA channel for ser4\n");
		break;
	case dma_jpeg:
		if (dmanr == 9)
			strmux_cfg.dma9 = regk_strmux_jpeg;
		else
			panic("Invalid DMA channel for JPEG\n");
		break;
	case dma_h264:
		if (dmanr == 11)
			strmux_cfg.dma11 = regk_strmux_h264;
		else
			panic("Invalid DMA channel for H264\n");
		break;
	}

	used_dma_channels[dmanr] = 1;
	used_dma_channels_users[dmanr] = device_id;
	REG_WR(clkgen, regi_clkgen, rw_clk_ctrl, clk_ctrl);
	REG_WR(strmux, regi_strmux, rw_cfg, strmux_cfg);
	spin_unlock_irqrestore(&dma_lock, flags);
	return 0;
}

void crisv32_free_dma(unsigned int dmanr)
{
	spin_lock(&dma_lock);
	used_dma_channels[dmanr] = 0;
	spin_unlock(&dma_lock);
}
