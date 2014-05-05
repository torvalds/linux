/*
 * Rockchip Specific Extensions for Synopsys DW Multimedia Card Interface driver
 *
 * Copyright (C) 2014, Rockchip Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/rk_mmc.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/slab.h>

#include "rk_sdmmc.h"
#include "dw_mmc-pltfm.h"
#include "../../clk/rockchip/clk-ops.h"

#include "rk_sdmmc_of.h"

/*CRU SDMMC TUNING*/
/*
*   sdmmc,sdio0,sdio1,emmc id=0~3
*   cclk_in_drv, cclk_in_sample  i=0,1
*/
#define CRU_SDMMC_CON(id, tuning_type)	(0x200 + ((id) * 8) + ((tuning_type) * 4))

#define SDMMC_TUNING_SEL(tuning_type)           ( tuning_type? 10:11 )
#define SDMMC_TUNING_DELAYNUM(tuning_type)      ( tuning_type? 2:3 )
#define SDMMC_TUNING_DEGREE(tuning_type)        ( tuning_type? 0:1 )
#define SDMMC_TUNING_INIT_STATE                 (0)

#define SDMMC_SHIFT_DEGREE_0                 (0)
#define SDMMC_SHIFT_DEGREE_90                (1)
#define SDMMC_SHIFT_DEGREE_180               (2)
#define SDMMC_SHIFT_DEGREE_270               (3)


/* Variations in Rockchip specific dw-mshc controller */
enum dw_mci_rockchip_type {
	DW_MCI_TYPE_RK3188,
	DW_MCI_TYPE_RK3288,
};

/* Rockchip implementation specific driver private data */
struct dw_mci_rockchip_priv_data {
	enum dw_mci_rockchip_type		ctrl_type;
	u8				ciu_div;
	u32				sdr_timing;
	u32				ddr_timing;
	u32				cur_speed;
};

static struct dw_mci_rockchip_compatible {
	char				*compatible;
	enum dw_mci_rockchip_type		ctrl_type;
} rockchip_compat[] = {
	{
		.compatible	= "rockchip,rk31xx-sdmmc",
		.ctrl_type	= DW_MCI_TYPE_RK3188,
	},{
		.compatible	= "rockchip,rk32xx-sdmmc",
		.ctrl_type	= DW_MCI_TYPE_RK3288,
	},
};

static int dw_mci_rockchip_priv_init(struct dw_mci *host)
{
	struct dw_mci_rockchip_priv_data *priv;
	int idx;

	priv = devm_kzalloc(host->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		dev_err(host->dev, "mem alloc failed for private data\n");
		return -ENOMEM;
	}

	for (idx = 0; idx < ARRAY_SIZE(rockchip_compat); idx++) {
		if (of_device_is_compatible(host->dev->of_node,
					rockchip_compat[idx].compatible))
			priv->ctrl_type = rockchip_compat[idx].ctrl_type;
	}

	host->priv = priv;
	return 0;
}

static int dw_mci_rockchip_setup_clock(struct dw_mci *host)
{
	struct dw_mci_rockchip_priv_data *priv = host->priv;

	if (priv->ctrl_type == DW_MCI_TYPE_RK3288)
		host->bus_hz /= (priv->ciu_div + 1);

	return 0;
}

static void dw_mci_rockchip_prepare_command(struct dw_mci *host, u32 *cmdr)
{
//	if (SDMMC_CLKSEL_GET_DRV_WD3(mci_readl(host, CLKSEL)))
//		*cmdr |= SDMMC_CMD_USE_HOLD_REG;
}

static void dw_mci_rockchip_set_ios(struct dw_mci *host, struct mmc_ios *ios)
{

}

static int dw_mci_rockchip_parse_dt(struct dw_mci *host)
{

	return 0;
}
static inline u8 dw_mci_rockchip_get_delaynum(struct dw_mci *host, u8 con_id, u8 tuning_type)
{
    u32 regs;
    u8 delaynum;
	regs =  cru_readl(CRU_SDMMC_CON(con_id, tuning_type)) ;
	delaynum = (regs>>SDMMC_TUNING_DELAYNUM(tuning_type)) & 0xff;

	return delaynum;
}

static inline void dw_mci_rockchip_set_delaynum(struct dw_mci *host, u8 con_id, u8 tuning_type, u8 delaynum)
{
    u32 regs;
	regs = cru_readl(CRU_SDMMC_CON(con_id, tuning_type)) ;
	regs &= ~( 0xff << SDMMC_TUNING_DELAYNUM(tuning_type));
	regs |= (delaynum  << SDMMC_TUNING_DELAYNUM(tuning_type));
	regs |= (0xff  << (SDMMC_TUNING_DELAYNUM(tuning_type)+16));
    MMC_DBG_INFO_FUNC(host->mmc,"tuning_result: con_id=%d, tuning_type=%d,SDMMC_CON=0x%x. [%s]",
        con_id,tuning_type,regs, mmc_hostname(host->mmc));	
    cru_writel(regs, CRU_SDMMC_CON(con_id, tuning_type));
}

static inline void dw_mci_rockchip_set_degree(struct dw_mci *host, u8 con_id, u8 tuning_type, u8 phase)
{
    u32 regs;
	regs = cru_readl(CRU_SDMMC_CON(con_id, tuning_type)) ;
	regs &= ~( 0x3 << SDMMC_TUNING_DEGREE(tuning_type));
	regs |= (phase  << SDMMC_TUNING_DEGREE(tuning_type));
	regs |= (0x3  << (SDMMC_TUNING_DEGREE(tuning_type)+16));
    cru_writel(regs, CRU_SDMMC_CON(con_id, tuning_type));
}


static inline u8 dw_mci_rockchip_get_phase(struct dw_mci *host, u8 con_id, u8 tuning_type)
{
	return 0;
}

static inline u8 dw_mci_rockchip_move_next_clksmpl(struct dw_mci *host, u8 con_id, u8 tuning_type, u8 val)
{
    u32 regs;
    //u8 delaynum;
	regs = cru_readl(CRU_SDMMC_CON(con_id, tuning_type)) ;

	if(tuning_type) {
	    val = (regs>>SDMMC_TUNING_DELAYNUM(tuning_type)) & 0xff;
	}

	return val;
}


static u8 dw_mci_rockchip_get_best_clksmpl(u8 *candiates)
{
    u8 pos, i;
    u8 bestval =0;
    
    for(pos=31; pos>0;pos--)
        if(candiates[pos] != 0) {
            for(i=7; i>0; i--)
            {
                if(candiates[pos]& (1<<i))
                   bestval = pos*8+i; 
            }          
        }

    return bestval;
}

static int dw_mci_rockchip_execute_tuning(struct dw_mci_slot *slot, u32 opcode,
					struct dw_mci_tuning_data *tuning_data)
{
	struct dw_mci *host = slot->host;
	struct mmc_host *mmc = slot->mmc;
	const u8 *blk_pattern = tuning_data->blk_pattern;
	u8 *blk_test;
	unsigned int blksz = tuning_data->blksz;
	u8 start_smpl, smpl, pos,index;
	u8 candiates[32];
	u8 found = 0;
	int ret = 0;

	blk_test = kmalloc(blksz, GFP_KERNEL);
	if (!blk_test)
		return -ENOMEM;
		
    //be fixed to 90 degrees
	dw_mci_rockchip_set_degree(host, tuning_data->con_id, tuning_data->tuning_type, SDMMC_SHIFT_DEGREE_90);

    //start_smpl = dw_mci_rockchip_get_delaynum(host, tuning_data->con_id, tuning_data->tuning_type);
    start_smpl = 0;
    smpl = 0xff;
    
    for(pos=0; pos<32; pos++)
        candiates[pos] = 0;
        
	do {
		struct mmc_request mrq = {NULL};
		struct mmc_command cmd = {0};
		struct mmc_command stop = {0};
		struct mmc_data data = {0};
		struct scatterlist sg;

		cmd.opcode = opcode;
		cmd.arg = 0;
		cmd.flags = MMC_RSP_R1 | MMC_CMD_ADTC;

		stop.opcode = MMC_STOP_TRANSMISSION;
		stop.arg = 0;
		stop.flags = MMC_RSP_R1B | MMC_CMD_AC;

		data.blksz = blksz;
		data.blocks = 1;
		data.flags = MMC_DATA_READ;
		data.sg = &sg;
		data.sg_len = 1;

		sg_init_one(&sg, blk_test, blksz);
		mrq.cmd = &cmd;
		mrq.stop = &stop;
		mrq.data = &data;
		host->mrq = &mrq;

		mci_writel(host, TMOUT, ~0);
		//smpl = smpl >> 1;//smpl = dw_mci_rockchip_move_next_clksmpl(host);

		mmc_wait_for_req(mmc, &mrq);

		if (!cmd.error && !data.error) {
			if (!memcmp(blk_pattern, blk_test, blksz)) {
			    pos = smpl/8;
			    index = smpl%8;
			    candiates[pos] |= (1 << index);

			    //temporary settings,!!!!!!!!!!!!!!!
			    if(smpl<64)
			        break;
			}				
		} else {
			dev_dbg(host->dev,
				"Tuning error: cmd.error:%d, data.error:%d\n",
				cmd.error, data.error);
		}
		smpl = smpl >> 1;
	} while (start_smpl != smpl);

	found = dw_mci_rockchip_get_best_clksmpl((u8 *)&candiates[0]);
	if (found >= 0)
		dw_mci_rockchip_set_delaynum(host, tuning_data->con_id, tuning_data->tuning_type, found);//dw_mci_rockchip_set_clksmpl(host, found);
	else
		ret = -EIO;

	kfree(blk_test);
	return ret;
}

/* Common capabilities of RK32XX SoC */
static unsigned long rockchip_dwmmc_caps[4] = {
	MMC_CAP_CMD23,
	MMC_CAP_CMD23,
	MMC_CAP_CMD23,
	MMC_CAP_CMD23,
};

unsigned int  rockchip_dwmmc_hold_reg[4] = {1,0,0,0};

static const struct dw_mci_drv_data rockchip_drv_data = {
	.caps			= rockchip_dwmmc_caps,
	.hold_reg_flag  = rockchip_dwmmc_hold_reg,
	.init			= dw_mci_rockchip_priv_init,
	.setup_clock		= dw_mci_rockchip_setup_clock,
	.prepare_command	= dw_mci_rockchip_prepare_command,
	.set_ios		= dw_mci_rockchip_set_ios,
	.parse_dt		= dw_mci_rockchip_parse_dt,
	.execute_tuning		= dw_mci_rockchip_execute_tuning,
};

static const struct of_device_id dw_mci_rockchip_match[] = {
	{ .compatible = "rockchip,rk_mmc",
			.data = &rockchip_drv_data, },
	{ /* Sentinel */},
};
MODULE_DEVICE_TABLE(of, dw_mci_rockchip_match);

#if DW_MMC_OF_PROBE
extern void rockchip_mmc_of_probe(struct device_node *np,struct rk_sdmmc_of *mmc_property);
#endif

static int dw_mci_rockchip_probe(struct platform_device *pdev)
{
	const struct dw_mci_drv_data *drv_data;
	const struct of_device_id *match;
	
	#if DW_MMC_OF_PROBE
    struct device_node *np = pdev->dev.of_node;
	struct rk_sdmmc_of *rk_mmc_property = NULL;

    rk_mmc_property = (struct rk_sdmmc_of *)kmalloc(sizeof(struct rk_sdmmc_of),GFP_KERNEL);
    if(NULL == rk_mmc_property)
    {
        kfree(rk_mmc_property);
        rk_mmc_property = NULL;
        printk("rk_mmc_property malloc space failed!\n");
        return 0;
    }
    
    rockchip_mmc_of_probe(np,rk_mmc_property);
    #endif /*DW_MMC_OF_PROBE*/
    

	match = of_match_node(dw_mci_rockchip_match, pdev->dev.of_node);
	drv_data = match->data;
	return dw_mci_pltfm_register(pdev, drv_data);
}

static struct platform_driver dw_mci_rockchip_pltfm_driver = {
	.probe		= dw_mci_rockchip_probe,
	.remove		= __exit_p(dw_mci_pltfm_remove),
	.driver		= {
		.name		= "dwmmc_rockchip",
		.of_match_table	= dw_mci_rockchip_match,
		.pm		= &dw_mci_pltfm_pmops,
	},
};

module_platform_driver(dw_mci_rockchip_pltfm_driver);

MODULE_DESCRIPTION("Rockchip Specific DW-SDMMC Driver Extension");
MODULE_AUTHOR("Bangwang Xie < xbw@rock-chips.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:dwmmc-rockchip");
