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
#include <linux/rockchip/cpu.h>
#include <linux/rockchip/cru.h>

#include "rk_sdmmc.h"
#include "dw_mmc-pltfm.h"
#include "../../clk/rockchip/clk-ops.h"

#include "rk_sdmmc_dbg.h"

/*CRU SDMMC TUNING*/
/*
*   sdmmc,sdio0,sdio1,emmc id=0~3
*   cclk_in_drv, cclk_in_sample  i=0,1
*/

static  u32 cru_tuning_base = 0;

#define CRU_SDMMC_CON(id, tuning_type)	(cru_tuning_base + ((id) * 8) + ((tuning_type) * 4))

#define MAX_DELAY_LINE  (0xff)
#define FREQ_REF_150MHZ (150000000)
#define PRECISE_ADJUST  (0)

#define SDMMC_TUNING_SEL(tuning_type)           ( tuning_type? 10:11 )
#define SDMMC_TUNING_DELAYNUM(tuning_type)      ( tuning_type? 2:3 )
#define SDMMC_TUNING_DEGREE(tuning_type)        ( tuning_type? 0:1 )
#define SDMMC_TUNING_INIT_STATE                 (0)

enum{
       SDMMC_SHIFT_DEGREE_0 = 0,
       SDMMC_SHIFT_DEGREE_90,
       SDMMC_SHIFT_DEGREE_180,
       SDMMC_SHIFT_DEGREE_270,
       SDMMC_SHIFT_DEGREE_INVALID,
};

const char *phase_desc[SDMMC_SHIFT_DEGREE_INVALID + 1] = {
        "SDMMC_SHIFT_DEGREE_0",
        "SDMMC_SHIFT_DEGREE_90",
        "SDMMC_SHIFT_DEGREE_180",
        "SDMMC_SHIFT_DEGREE_270",
        "SDMMC_SHIFT_DEGREE_INVALID",
};

enum{
        USE_CLK_AFTER_PHASE = 0,
        USE_CLK_AFTER_PHASE_AND_DELAY_LINE = 1,
};

enum{
        IO_DRV_2MA  = 0x0,
        IO_DRV_4MA  = 0x1,
        IO_DRV_8MA  = 0x2,
        IO_DRV_12MA = 0x3,
};

enum{
        SLEW_RATE_SLOW = 0,
        SLEW_RATE_FAST = 1,
};

/* Variations in Rockchip specific dw-mshc controller */
enum dw_mci_rockchip_type {
	DW_MCI_TYPE_RK3188,
	DW_MCI_TYPE_RK3288,
	DW_MCI_TYPE_RK3036,
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
	},{
		.compatible	= "rockchip,rk3036-sdmmc",
		.ctrl_type	= DW_MCI_TYPE_RK3036,
	},
};

static int dw_mci_rockchip_priv_init(struct dw_mci *host)
{
	struct dw_mci_rockchip_priv_data *priv;
	int idx;

	priv = devm_kzalloc(host->dev, sizeof(*priv), GFP_KERNEL);
	if(!priv){
		dev_err(host->dev, "mem alloc failed for private data\n");
		return -ENOMEM;
	}

	for(idx = 0; idx < ARRAY_SIZE(rockchip_compat); idx++){
                if(of_device_is_compatible(host->dev->of_node,
                                rockchip_compat[idx].compatible))
			priv->ctrl_type = rockchip_compat[idx].ctrl_type;
	}

	host->priv = priv;
	return 0;
}

static int dw_mci_rockchip_setup_clock(struct dw_mci *host)
{
	struct dw_mci_rockchip_priv_data *priv = host->priv;

	if ((priv->ctrl_type == DW_MCI_TYPE_RK3288) ||
	        (priv->ctrl_type == DW_MCI_TYPE_RK3036))
		host->bus_hz /= (priv->ciu_div + 1);

	return 0;
}

static void dw_mci_rockchip_prepare_command(struct dw_mci *host, u32 *cmdr)
{

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

        regs =  cru_readl(CRU_SDMMC_CON(con_id, tuning_type));
        delaynum = ((regs>>SDMMC_TUNING_DELAYNUM(tuning_type)) & 0xff);

        return delaynum;
}

static inline void dw_mci_rockchip_set_delaynum(struct dw_mci *host, u8 con_id, u8 tuning_type, u8 delaynum)
{
        u32 regs;
        regs = cru_readl(CRU_SDMMC_CON(con_id, tuning_type));
        regs &= ~( 0xff << SDMMC_TUNING_DELAYNUM(tuning_type));
        regs |= (delaynum  << SDMMC_TUNING_DELAYNUM(tuning_type));
        regs |= (0xff  << (SDMMC_TUNING_DELAYNUM(tuning_type)+16));

        MMC_DBG_INFO_FUNC(host->mmc,"tuning_result[delayline]: con_id = %d, tuning_type = %d, CRU_CON = 0x%x. [%s]",
                con_id, tuning_type, regs, mmc_hostname(host->mmc));

        cru_writel(regs, CRU_SDMMC_CON(con_id, tuning_type));
}

static inline void dw_mci_rockchip_set_degree(struct dw_mci *host, u8 con_id, u8 tuning_type, u8 phase)
{
        u32 regs;
        
	regs = cru_readl(CRU_SDMMC_CON(con_id, tuning_type));
	regs &= ~( 0x3 << SDMMC_TUNING_DEGREE(tuning_type));
	regs |= (phase  << SDMMC_TUNING_DEGREE(tuning_type));
	regs |= (0x3  << (SDMMC_TUNING_DEGREE(tuning_type)+16));

        MMC_DBG_INFO_FUNC(host->mmc,"tuning_result[phase]: con_id = %d, tuning_type= %d, CRU_CON = 0x%x. [%s]",
                con_id, tuning_type, regs, mmc_hostname(host->mmc));
	
	cru_writel(regs, CRU_SDMMC_CON(con_id, tuning_type));
}

static inline void dw_mci_rockchip_turning_sel(struct dw_mci *host, u8 con_id, u8 tuning_type, u8 mode)
{
        u32 regs;
	regs = cru_readl(CRU_SDMMC_CON(con_id, tuning_type)) ;
	regs &= ~( 0x1 << SDMMC_TUNING_SEL(tuning_type));
	regs |= (mode  << SDMMC_TUNING_SEL(tuning_type));
	regs |= (0x1  << (SDMMC_TUNING_SEL(tuning_type)+16));

	MMC_DBG_INFO_FUNC(host->mmc,"tuning_sel: con_id = %d, tuning_type = %d, CRU_CON = 0x%x. [%s]",
                con_id, tuning_type, regs, mmc_hostname(host->mmc));
                
	cru_writel(regs, CRU_SDMMC_CON(con_id, tuning_type));       
}


static inline u8 dw_mci_rockchip_get_phase(struct dw_mci *host, u8 con_id, u8 tuning_type)
{
	return 0;
}

static inline u8 dw_mci_rockchip_move_next_clksmpl(struct dw_mci *host, u8 con_id, u8 tuning_type, u8 val)
{
        u32 regs;
        
        regs = cru_readl(CRU_SDMMC_CON(con_id, tuning_type)) ;

	if(tuning_type) {
	    val = ((regs>>SDMMC_TUNING_DELAYNUM(tuning_type)) & 0xff);
	}

	return val;
}

static void dw_mci_rockchip_load_signal_integrity(struct dw_mci *host, u32 sr, u32 drv)
{
        if (unlikely((drv > IO_DRV_12MA) || (sr > SLEW_RATE_FAST))) {
                MMC_DBG_ERR_FUNC(host->mmc,"wrong signal integrity setting: drv = %d, sr = %d ![%s]",
                        drv, sr, mmc_hostname(host->mmc));
                return;
        }

        if(cpu_is_rk3288()){
                /*Note 00: 2ma 01:4ma 10:8ma 11:12ma
                For consider line loading and IP's slew rate,
                we should match these by every board depends for signal integrity.
                slew rate >= 2*pi*f*Vpeak = max(|d'(Vpeak)/dt|)
                */
                if (host->mmc->restrict_caps & RESTRICT_CARD_TYPE_SDIO) {
                        grf_writel(0xff005500 | (drv << 14) | (drv << 12) |
                                                 (drv << 10) | (drv << 8), 0x01f8); /* GPIO4C4-C7 */
                        grf_writel(0x000f0000 | (drv << 0) | (drv << 2), 0x01fc); /* GPIO4D0-D1 */
                        grf_writel(0x03f00000 | (sr << 4) | (sr << 5) | (sr << 6) |
                                                (sr << 7) | (sr << 8) | (sr << 9) , 0x011c); /* slew rate*/
                }else if (host->mmc->restrict_caps & RESTRICT_CARD_TYPE_SD) {
                        grf_writel(0x3fff0000 | (drv << 0) | (drv << 2) | (drv << 4) |
                                                 (drv << 6) | (drv << 8) | (drv << 10) |
                                                 (drv << 12), 0x0218); /* GPIO6C0-C6 */
                        grf_writel(0x003f0000 | (sr << 0) | (sr << 1) | (sr << 2) |
                                                (sr << 3) | (sr << 4) | (sr << 5), 0x012c); /* slew rate */
                }else if (host->mmc->restrict_caps & RESTRICT_CARD_TYPE_EMMC) {
                        /* emmc hardware relative addr match requirement, assume 4ma not slow slew rate */
                        grf_writel(0xffff5555, 0x01e0); /* GPIO3A0-A7 */
                        grf_writel(0x000c0006, 0x01e4); /* GPIO3B1 */
                        grf_writel(0x003f0015, 0x01e8); /* GPIO3C2-C0 */
                }
        }

}
static void dw_mci_rockchip_load_tuning_base(void)
{
        /* load tuning base */
        if(cpu_is_rk3288())
                cru_tuning_base =  RK3288_CRU_SDMMC_CON0;

     /* Fixme: 3036
        else if(cpu_is_rk3036())
                cru_tuning_base =  RK3036_CRU_SDMMC_CON0;
     */
}

static int inline __dw_mci_rockchip_execute_tuning(struct dw_mci_slot *slot, u32 opcode,
					u8 *blk_test, unsigned int blksz)
{
        struct dw_mci *host = slot->host;
	struct mmc_host *mmc = slot->mmc;	
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

	mmc_wait_for_req(mmc, &mrq);
	if(!cmd.error && !data.error){
                return 0;
        }else{
                dev_dbg(host->dev,
	                "Tuning error: cmd.error:%d, data.error:%d\n",cmd.error, data.error);
	        return -EIO;
	}
	
}


static int dw_mci_rockchip_execute_tuning(struct dw_mci_slot *slot, u32 opcode,
					struct dw_mci_tuning_data *tuning_data)
{
        
	struct dw_mci *host = slot->host;
	u8 step;
	u8 candidates_delayline[MAX_DELAY_LINE] = {0};
	u8 candidates_degree[SDMMC_SHIFT_DEGREE_INVALID] = {4,4,4,4};
	u8 default_drv;
	u8 index = 0;
	u8 start_degree = 0;
	u32 start_delayline = 0;
	const u8 *blk_pattern = tuning_data->blk_pattern;
	u8 *blk_test;
	int ret = -1;
	int ref = 0;
	unsigned int blksz = tuning_data->blksz;

	MMC_DBG_INFO_FUNC(host->mmc,"execute tuning:  [%s]", mmc_hostname(host->mmc));

	dw_mci_rockchip_load_tuning_base();

	blk_test = kmalloc(blksz, GFP_KERNEL);
	if (!blk_test)
	{
	        MMC_DBG_ERR_FUNC(host->mmc,"execute tuning:  blk_test kmalloc failed[%s]",
	                mmc_hostname(host->mmc));
		return -ENOMEM;
        }
        
        /* Select use delay line*/
        dw_mci_rockchip_turning_sel(host, tuning_data->con_id, tuning_data->tuning_type,
                                    USE_CLK_AFTER_PHASE_AND_DELAY_LINE);
                                    
        /* For RK32XX signoff 150M clk, 1 cycle = 6.66ns , and 1/4 phase = 1.66ns. 
           Netlist level sample LT:  10.531ns / 42.126ps   WC: 19.695ns / 76.936ps.
           So we take average --- 60ps, (1.66ns/ 2) = 0.83(middle-value),TAKE 0.9
           0.9 / 60ps = 15 delayline
         */
        if(cpu_is_rk3288()){
                /* Fixme: 3036:  dose it compatitable? */
                 ref = ((FREQ_REF_150MHZ + host->bus_hz - 1) / host->bus_hz);
                 step = (15 * ref);

                 if(step > MAX_DELAY_LINE){
                        step = MAX_DELAY_LINE;       
                        MMC_DBG_WARN_FUNC(host->mmc,
                                        "execute tuning: TOO LARGE STEP![%s]", mmc_hostname(host->mmc));
                 }              
                 MMC_DBG_INFO_FUNC(host->mmc,
                                "execute tuning: SOC is RK3288, ref = %d, step = %d[%s]",
                                ref, step, mmc_hostname(host->mmc));
                 
        }else{              
                 step = (15 * ((FREQ_REF_150MHZ / host->bus_hz) * 100)) / 100;

                 if(step > MAX_DELAY_LINE){
                        step = MAX_DELAY_LINE;
                        MMC_DBG_WARN_FUNC(host->mmc,
                                        "execute tuning: TOO LARGE STEP![%s]", mmc_hostname(host->mmc));
                 }              
                 MMC_DBG_INFO_FUNC(host->mmc,
                                "execute tuning: SOC is UNKNOWN, step = %d[%s]",
                                step, mmc_hostname(host->mmc));
        }

re_phase:
        /* calcute slew rate & drv strength in timing tuning */
        if(host->mmc->restrict_caps & RESTRICT_CARD_TYPE_SD)
                default_drv = IO_DRV_4MA;
        else
                default_drv = IO_DRV_8MA;

        dw_mci_rockchip_load_signal_integrity(host, SLEW_RATE_SLOW, default_drv);
        /* Loop degree from 0 ~ 270 */
        for(start_degree = SDMMC_SHIFT_DEGREE_0; start_degree < SDMMC_SHIFT_DEGREE_270; start_degree++){

                dw_mci_rockchip_set_degree(host, tuning_data->con_id, tuning_data->tuning_type, start_degree);
                if(0 == __dw_mci_rockchip_execute_tuning(slot, opcode, blk_test, blksz)){
                        if(!memcmp(blk_pattern, blk_test, blksz)){
                                /* Successfully tuning in this condition*/                      
                                candidates_degree[index] = start_degree;
                                index++;
                         }
               }              
                
        }
        
        MMC_DBG_INFO_FUNC(host->mmc,"\n execute tuning: candidates_degree = %s \t%s \t%s \t%s[%s]",
                phase_desc[candidates_degree[0]], phase_desc[candidates_degree[1]],
                phase_desc[candidates_degree[2]], phase_desc[candidates_degree[3]],
                mmc_hostname(host->mmc));

        
        if((candidates_degree[0] == SDMMC_SHIFT_DEGREE_0)
                && (candidates_degree[1] == SDMMC_SHIFT_DEGREE_90)
                && (candidates_degree[2] == SDMMC_SHIFT_DEGREE_180)){
           
                MMC_DBG_INFO_FUNC(host->mmc,
                                "execute tuning: candidates_degree = SDMMC_SHIFT_DEGREE_90 [%s]",
                                mmc_hostname(host->mmc));
                                
                dw_mci_rockchip_set_degree(host, tuning_data->con_id, 
                        tuning_data->tuning_type, SDMMC_SHIFT_DEGREE_90);
                ret = 0;
                goto done;
        }else if((candidates_degree[0] == SDMMC_SHIFT_DEGREE_90) 
                && (candidates_degree[1] == SDMMC_SHIFT_DEGREE_180) 
                && (candidates_degree[2] == SDMMC_SHIFT_DEGREE_270)){
                MMC_DBG_INFO_FUNC(host->mmc,
                        "execute tuning: candidates_degree = SDMMC_SHIFT_DEGREE_180 [%s]",
                        mmc_hostname(host->mmc));
                dw_mci_rockchip_set_degree(host, tuning_data->con_id, 
                        tuning_data->tuning_type, SDMMC_SHIFT_DEGREE_180);
                ret = 0;
                goto done;
        }else if((candidates_degree[0] == SDMMC_SHIFT_DEGREE_0) 
                && (candidates_degree[1] == SDMMC_SHIFT_DEGREE_90) 
                && (candidates_degree[2] == SDMMC_SHIFT_DEGREE_INVALID)){

                MMC_DBG_INFO_FUNC(host->mmc,
                        "execute tuning: candidates_degree = SDMMC_SHIFT_DEGREE_0 ~  SDMMC_SHIFT_DEGREE_90[%s]",
                        mmc_hostname(host->mmc));
                
                dw_mci_rockchip_set_degree(host, tuning_data->con_id, tuning_data->tuning_type, SDMMC_SHIFT_DEGREE_0);
                #if PRECISE_ADJUST
                goto delayline; 
                #else              
                dw_mci_rockchip_set_delaynum(host, tuning_data->con_id, tuning_data->tuning_type, step);
                ret = 0;
		goto done;  
                #endif       
        }else if((candidates_degree[0]==SDMMC_SHIFT_DEGREE_0) 
                && (candidates_degree[1]==SDMMC_SHIFT_DEGREE_180)){

                MMC_DBG_INFO_FUNC(host->mmc,
                        "execute tuning: candidates_degree = SDMMC_SHIFT_DEGREE_0 AND SDMMC_SHIFT_DEGREE_180[%s]",
                        mmc_hostname(host->mmc));

                /* FixMe: NO sense any signal indicator make this case happen*/
                dw_mci_rockchip_set_degree(host, tuning_data->con_id, tuning_data->tuning_type, SDMMC_SHIFT_DEGREE_0);
                goto delayline;
        }else if((candidates_degree[0] == SDMMC_SHIFT_DEGREE_90) 
                && (candidates_degree[1] == SDMMC_SHIFT_DEGREE_180) 
                && (candidates_degree[2] == SDMMC_SHIFT_DEGREE_INVALID)){

                MMC_DBG_INFO_FUNC(host->mmc,
                        "execute tuning: candidates_degree = SDMMC_SHIFT_DEGREE_90 ~  SDMMC_SHIFT_DEGREE_180[%s]",
                        mmc_hostname(host->mmc));
               
                dw_mci_rockchip_set_degree(host, tuning_data->con_id, tuning_data->tuning_type, SDMMC_SHIFT_DEGREE_90);
                #if PRECISE_ADJUST
                goto delayline; 
                #else              
                dw_mci_rockchip_set_delaynum(host, tuning_data->con_id, tuning_data->tuning_type, step);
                ret = 0;
		goto done;  
                #endif           	            
        }else if((candidates_degree[0] == SDMMC_SHIFT_DEGREE_180) 
                && (candidates_degree[1] == SDMMC_SHIFT_DEGREE_270)){

                MMC_DBG_INFO_FUNC(host->mmc,
                        "execute tuning: candidates_degree = SDMMC_SHIFT_DEGREE_180 ~  SDMMC_SHIFT_DEGREE_270[%s]",
                        mmc_hostname(host->mmc));
              		
                dw_mci_rockchip_set_degree(host, tuning_data->con_id, tuning_data->tuning_type, SDMMC_SHIFT_DEGREE_180);
                #if PRECISE_ADJUST
                goto delayline; 
                #else              
                dw_mci_rockchip_set_delaynum(host, tuning_data->con_id, tuning_data->tuning_type, step);
                ret = 0;
		goto done;
                #endif
        }else if((candidates_degree[0] == SDMMC_SHIFT_DEGREE_180) 
                && (candidates_degree[1] == SDMMC_SHIFT_DEGREE_INVALID)){

                MMC_DBG_INFO_FUNC(host->mmc,
                        "execute tuning: candidates_degree = [SDMMC_SHIFT_DEGREE_90 + n ~  SDMMC_SHIFT_DEGREE_180][%s]",
                        mmc_hostname(host->mmc));             
                
                dw_mci_rockchip_set_degree(host, tuning_data->con_id, tuning_data->tuning_type, SDMMC_SHIFT_DEGREE_90);
                #if PRECISE_ADJUST
                goto delayline;
                #else
                default_drv++;
                goto re_phase;
                //dw_mci_rockchip_set_delaynum(host, tuning_data->con_id, tuning_data->tuning_type, step);
                //ret = 0;
		//goto done;
                #endif
        }else if((candidates_degree[0] == SDMMC_SHIFT_DEGREE_90) 
                && (candidates_degree[1] == SDMMC_SHIFT_DEGREE_INVALID)){

                MMC_DBG_INFO_FUNC(host->mmc,
                        "execute tuning: candidates_degree = [SDMMC_SHIFT_DEGREE_0 + n ~  SDMMC_SHIFT_DEGREE_90][%s]",
                        mmc_hostname(host->mmc));             
                
                dw_mci_rockchip_set_degree(host, tuning_data->con_id, tuning_data->tuning_type, SDMMC_SHIFT_DEGREE_0);
                #if PRECISE_ADJUST
                goto delayline; 
                #else
                default_drv++;
                goto re_phase;
                //dw_mci_rockchip_set_delaynum(host, tuning_data->con_id, tuning_data->tuning_type, step);
                //ret = 0;
		//goto done;
                #endif
        }else if((candidates_degree[0] == SDMMC_SHIFT_DEGREE_270)){

                MMC_DBG_INFO_FUNC(host->mmc,
                        "execute tuning: candidates_degree = SDMMC_SHIFT_DEGREE_270 [%s]",
                        mmc_hostname(host->mmc));         

                /*FixME: so urgly signal indicator, HW engineer help!*/

                //dw_mci_rockchip_set_degree(host, tuning_data->con_id, tuning_data->tuning_type, SDMMC_SHIFT_DEGREE_180);
                #if PRECISE_ADJUST
                goto delayline; 
                #else
                default_drv++;
                goto re_phase;
                //dw_mci_rockchip_set_delaynum(host, tuning_data->con_id, tuning_data->tuning_type, step);
                //ret = 0;
		//goto done;
                #endif            
        }else{
                MMC_DBG_ERR_FUNC(host->mmc,
                                "execute tuning: candidates_degree beyong limited case! [%s]",
                                mmc_hostname(host->mmc));
                default_drv++;
                goto re_phase;
                if(host->mmc->restrict_caps & RESTRICT_CARD_TYPE_EMMC)
                        BUG();
                return -EAGAIN;
        }

delayline:
                index = 0;
                for(start_delayline = 0; start_delayline <= MAX_DELAY_LINE; start_delayline += step){
                
                        dw_mci_rockchip_set_delaynum(host, tuning_data->con_id, 
                                tuning_data->tuning_type, start_delayline);
                        if(0 == __dw_mci_rockchip_execute_tuning(slot, opcode, blk_test, blksz)){
                                if(!memcmp(blk_pattern, blk_test, blksz)){
                                        /* Successfully tuning in this condition*/                                        
                                        candidates_delayline[index] = start_delayline;
                                        index++; 
                                }
                        }                    
                }
                if((index < 2) && (index != 0)) {
                        /* setup 400ps, consider line loading, at least 600ps wc.
                           for 150M, 15 steps =900ps ,too larger scale, should step smaller in principle
                         */
                        MMC_DBG_INFO_FUNC(host->mmc,
                                "execute tuning: candidates_delayline failed for no enough elements [%s]",
                                mmc_hostname(host->mmc));

                        /* Make step smaller, and re-calculate */
                        step = step >> 1;
                        index = 0;
                        goto delayline;
                }else if(index >= 2){
                        /* Find it! */
                        MMC_DBG_INFO_FUNC(host->mmc,
                                "execute tuning: candidates_delayline calculate successfully  [%s]",
                                mmc_hostname(host->mmc));

                        dw_mci_rockchip_set_delaynum(host, tuning_data->con_id, 
                                tuning_data->tuning_type, candidates_delayline[index/2]); 
                        ret = 0; 
                        goto done;
                }
        
done:
        kfree(blk_test);
        blk_test = NULL;
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

static int dw_mci_rockchip_probe(struct platform_device *pdev)
{
	const struct dw_mci_drv_data *drv_data;
	const struct of_device_id *match;
	
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
MODULE_AUTHOR("Shawn Lin <lintao@rock-chips.com>");
MODULE_AUTHOR("Bangwang Xie <xbw@rock-chips.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:dwmmc-rockchip");
