/*
 * drivers/amlogic/mmc/aml_sdhc.c
 *
 * SDHC Driver
 *
 * Copyright (C) 2010 Amlogic, Inc.
*/
#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/timer.h>
#include <linux/clk.h>
#include <linux/mmc/host.h>
#include <linux/mmc/card.h>
#include <linux/io.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/sdio.h>
#include <linux/highmem.h>
#include <linux/slab.h>
#include <asm/dma-mapping.h>
#include <mach/power_gate.h>
#include <linux/clk.h>
#include <mach/register.h>
// #include <mach/gpio.h>
#include <mach/pinmux.h>
#include <mach/irqs.h>
#include <linux/irq.h>
#include <mach/sd.h>
#include <mach/mod_gate.h>
#include "amlsd.h"

//#define DMC_URGENT_PERIPH
unsigned int rx_clk_phase_set=1;
unsigned int sd_clk_phase_set=1;
unsigned int rx_endian=7;
unsigned int tx_endian=7;
unsigned int val1=0;
unsigned int cmd25_cnt=0;
unsigned int fifo_empty_cnt=0;
unsigned int fifo_full_cnt=0;
unsigned int timeout_cnt=0;
static unsigned int sdhc_error_flag = 0;
static unsigned int sdhc_debug_flag = 0;
static int sdhc_err_bak;

void aml_sdhc_send_stop(struct amlsd_host* host);

static void aml_sdhc_clk_switch (struct amlsd_platform* pdata, int clk_div, int clk_src_sel);
static int aml_sdhc_status (struct amlsd_host* host);

static const u8 tuning_blk_pattern_4bit[] = {
	0xff, 0x0f, 0xff, 0x00, 0xff, 0xcc, 0xc3, 0xcc,
	0xc3, 0x3c, 0xcc, 0xff, 0xfe, 0xff, 0xfe, 0xef,
	0xff, 0xdf, 0xff, 0xdd, 0xff, 0xfb, 0xff, 0xfb,
	0xbf, 0xff, 0x7f, 0xff, 0x77, 0xf7, 0xbd, 0xef,
	0xff, 0xf0, 0xff, 0xf0, 0x0f, 0xfc, 0xcc, 0x3c,
	0xcc, 0x33, 0xcc, 0xcf, 0xff, 0xef, 0xff, 0xee,
	0xff, 0xfd, 0xff, 0xfd, 0xdf, 0xff, 0xbf, 0xff,
	0xbb, 0xff, 0xf7, 0xff, 0xf7, 0x7f, 0x7b, 0xde,
};

static const u8 tuning_blk_pattern_8bit[] = {
	0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0x00,
	0xff, 0xff, 0xcc, 0xcc, 0xcc, 0x33, 0xcc, 0xcc,
	0xcc, 0x33, 0x33, 0xcc, 0xcc, 0xcc, 0xff, 0xff,
	0xff, 0xee, 0xff, 0xff, 0xff, 0xee, 0xee, 0xff,
	0xff, 0xff, 0xdd, 0xff, 0xff, 0xff, 0xdd, 0xdd,
	0xff, 0xff, 0xff, 0xbb, 0xff, 0xff, 0xff, 0xbb,
	0xbb, 0xff, 0xff, 0xff, 0x77, 0xff, 0xff, 0xff,
	0x77, 0x77, 0xff, 0x77, 0xbb, 0xdd, 0xee, 0xff,
	0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00,
	0x00, 0xff, 0xff, 0xcc, 0xcc, 0xcc, 0x33, 0xcc,
	0xcc, 0xcc, 0x33, 0x33, 0xcc, 0xcc, 0xcc, 0xff,
	0xff, 0xff, 0xee, 0xff, 0xff, 0xff, 0xee, 0xee,
	0xff, 0xff, 0xff, 0xdd, 0xff, 0xff, 0xff, 0xdd,
	0xdd, 0xff, 0xff, 0xff, 0xbb, 0xff, 0xff, 0xff,
	0xbb, 0xbb, 0xff, 0xff, 0xff, 0x77, 0xff, 0xff,
	0xff, 0x77, 0x77, 0xff, 0x77, 0xbb, 0xdd, 0xee,
};

static void  __attribute__((unused))sdhc_debug_status(struct amlsd_host* host)
{
    switch (sdhc_debug_flag)
    {
        case 1:
            host->status = HOST_TX_FIFO_EMPTY;
            sdhc_err("Force host->status:%d here\n", host->status);
            break;
        case 2:
            host->status = HOST_RX_FIFO_FULL;
            sdhc_err("Force host->status:%d here\n", host->status);
            break;  
        case 3:
            host->status = HOST_RSP_CRC_ERR;
            sdhc_err("Force host->status:%d here\n", host->status);
            break;  
        case 4:
            host->status = HOST_DAT_CRC_ERR;
            sdhc_err("Force host->status:%d here\n", host->status);
            break;              
        case 5:
            host->status = HOST_DAT_TIMEOUT_ERR;
            sdhc_err("Force host->status:%d here\n", host->status);
            break;   
        case 6:
            host->status = HOST_RSP_TIMEOUT_ERR;
            sdhc_err("Force host->status:%d here\n", host->status);
            break; 
                                    
        default:            
            break;
    }
    
    //only enable once for debug
    sdhc_debug_flag = 0;
}

struct aml_tuning_data {
	const u8 *blk_pattern;
	unsigned int blksz;
};

#if 0
//NAND RB pin
static void aml_sdhc_gpio_dbg_level(unsigned val)
{
    //clear pinmux
    aml_clr_reg32_mask(P_PERIPHS_PIN_MUX_2, (1<<17));
    
    //set output mode   
    aml_clr_reg32_mask(P_PREG_PAD_GPIO3_EN_N, (1<<10));
    
    //set output value
    if(val == 1)  
        aml_set_reg32_mask(P_PREG_PAD_GPIO3_O, (1<<10));
    else
        aml_clr_reg32_mask(P_PREG_PAD_GPIO3_O, (1<<10));         
}
#endif

void aml_debug_print_buf (char *buf, int size)
{
    int i;

    if (size > 512)
        size = 512;

    printk("%8s : ", "Address");
    for (i=0; i<16; i++) {
        printk("%02x ", i);
    }
    printk("\n");
    printk("==========================================================\n");

    for (i=0; i<size; i++) {
        if ((i % 16) == 0)
            printk("%08x : ", i);

        printk("%02x ", buf[i]);

        if ((i % 16) == 15)
            printk("\n");
    }
    printk("\n");
}

int aml_buf_verify (int *buf, int blocks, int lba)
{
    int block_size;
    int i, j;
    int lba_bak = lba;

    sdhc_err("Enter\n");
    for (i=0; i<blocks; i++) {
        for (j=0; j<128; j++) {
            if (buf[j] != (lba*512 + j)) {
                sdhc_err("buf is error, lba_bak=%#x, lba=%#x, offset=%#x, blocks=%d\n", lba_bak, lba, j, blocks);
                sdhc_err("buf[j]=%#x, target=%#x\n", buf[j], (lba*512 + j));
                block_size = (lba - lba_bak)*512;
                aml_debug_print_buf((char*)(buf+block_size), 512);

                return -1;
            }
        }
        lba++;
        buf += 128;
    }

    return 0;
}

static int aml_sdhc_execute_tuning_ (struct mmc_host *mmc, u32 opcode,
					struct aml_tuning_data *tuning_data)
{
	struct amlsd_platform *pdata = mmc_priv(mmc);
	struct amlsd_host *host = pdata->host;
	struct sdhc_clkc *clkc = (struct sdhc_clkc *)&(pdata->clkc);
	u32 vclk2, vclk2_bak;
	struct sdhc_clk2 *clk2 = (struct sdhc_clk2 *)&vclk2;
	const u8 *blk_pattern = tuning_data->blk_pattern;
	u8 *blk_test;
	unsigned int blksz = tuning_data->blksz;
	int ret = 0;
    unsigned long flags;

	int n, nmatch, ntries = 10;
	int rx_phase = 0;
	int wrap_win_start = -1, wrap_win_size = 0;
	int best_win_start = -1, best_win_size = -1;
	int curr_win_start = -1, curr_win_size = -1;

	u8 rx_tuning_result[20] = { 0 };

    spin_lock_irqsave(&host->mrq_lock, flags);
    pdata->need_retuning = false;
    spin_unlock_irqrestore(&host->mrq_lock, flags);

    vclk2_bak = readl(host->base + SDHC_CLK2);

	blk_test = kmalloc(blksz, GFP_KERNEL);
	if (!blk_test)
		return -ENOMEM;

	for (rx_phase = 0; rx_phase <= clkc->clk_div; rx_phase++) {
		// Perform tuning ntries times per clk_div increment
		for (n = 0, nmatch = 0; n < ntries; n++) {
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

			memset(blk_test, 0, blksz);
			sg_init_one(&sg, blk_test, blksz);

			mrq.cmd = &cmd;
			mrq.stop = &stop;
			mrq.data = &data;
			host->mrq = &mrq;

			vclk2 = readl(host->base + SDHC_CLK2);
			//clk2->sd_clk_phase = sd_clk_phase_set;
			clk2->rx_clk_phase = rx_phase;
			writel(vclk2, host->base + SDHC_CLK2);
			pdata->clk2 = vclk2;

			mmc_wait_for_req(mmc, &mrq);

			if (!cmd.error && !data.error) {
				if (!memcmp(blk_pattern, blk_test, blksz)) {
					nmatch++;
				}
				else {
                    sdhc_dbg(AMLSD_DBG_TUNING, "Tuning pattern mismatch: rx_phase=%d nmatch=%d\n", rx_phase, nmatch);
				}
			}
			else {
                sdhc_dbg(AMLSD_DBG_TUNING, "Tuning transfer error: rx_phase=%d nmatch=%d cmd.error=%d data.error=%d\n", 
                        rx_phase, nmatch, cmd.error, data.error);
			}
		}

		if (rx_phase < sizeof(rx_tuning_result) / sizeof (rx_tuning_result[0]))
			rx_tuning_result[rx_phase] = nmatch;

		if (nmatch == ntries) {
			if (rx_phase == 0)
				wrap_win_start = rx_phase;

			if (wrap_win_start >= 0)
				wrap_win_size++;

			if (curr_win_start < 0)
				curr_win_start = rx_phase;

			curr_win_size++;
		} else {
			if (curr_win_start >= 0) {
				if (best_win_start < 0) {
					best_win_start = curr_win_start;
					best_win_size = curr_win_size;
				}
				else {
					if (best_win_size < curr_win_size) {
						best_win_start = curr_win_start;
						best_win_size = curr_win_size;
					}
				}

				wrap_win_start = -1;
				curr_win_start = -1;
				curr_win_size = 0;
			}
		}
	}

    sdhc_dbg(AMLSD_DBG_TUNING, "RX Tuning Result\n");
	for (n = 0; n <= clkc->clk_div; n++) {
		if (n < sizeof(rx_tuning_result) / sizeof (rx_tuning_result[0]))
            sdhc_dbg(AMLSD_DBG_TUNING, "RX[%d]=%d\n", n, rx_tuning_result[n]);
	}

    sdhc_dbg(AMLSD_DBG_TUNING, "curr_win_start=%d\n", curr_win_start);
	sdhc_dbg(AMLSD_DBG_TUNING, "curr_win_size=%d\n", curr_win_size);
	sdhc_dbg(AMLSD_DBG_TUNING, "best_win_start=%d\n", best_win_start);
	sdhc_dbg(AMLSD_DBG_TUNING, "best_win_size=%d\n", best_win_size);
	sdhc_dbg(AMLSD_DBG_TUNING, "wrap_win_start=%d\n", wrap_win_start);
	sdhc_dbg(AMLSD_DBG_TUNING, "wrap_win_size=%d\n", wrap_win_size);

	if (curr_win_start >= 0) {
		if (best_win_start < 0) {
			best_win_start = curr_win_start;
			best_win_size = curr_win_size;
		}
		else if (wrap_win_size > 0) {
			// Wrap around case
			if (curr_win_size + wrap_win_size > best_win_size) {
				best_win_start = curr_win_start;
				best_win_size = curr_win_size + wrap_win_size;
			}
		}
		else if (best_win_size < curr_win_size) {
			best_win_start = curr_win_start;
			best_win_size = curr_win_size;
		}

		curr_win_start = -1;
		curr_win_size = 0;
	}


	if (best_win_start < 0) {
		sdhc_err("Tuning failed to find a valid window, using default rx phase\n");
		ret = -EIO;
        writel(vclk2_bak, host->base + SDHC_CLK2);
        pdata->clk2 = vclk2_bak;
		// rx_phase = rx_clk_phase_set;
	}
    else {
        pdata->is_tuned = true;

        rx_phase = best_win_start + (best_win_size / 2);

        if (rx_phase > clkc->clk_div)
            rx_phase -= (clkc->clk_div + 1);

        vclk2 = readl(host->base + SDHC_CLK2);
        //clk2->sd_clk_phase = sd_clk_phase_set;
        clk2->rx_clk_phase = rx_phase;
        writel(vclk2, host->base + SDHC_CLK2);
        pdata->clk2 = vclk2;
        pdata->tune_phase = vclk2;

        sdhc_dbg(AMLSD_DBG_TUNING, "Tuning result: rx_phase=%d\n", rx_phase);
        // printk("Tuning result: rx_phase=%d\n", rx_phase);
        // sdhc_err("Tuning result: rx_phase=%d, tune_phase=%#x\n", rx_phase, pdata->tune_phase);
    }

	sdhc_dbg(AMLSD_DBG_TUNING, "Final Result\n");
	sdhc_dbg(AMLSD_DBG_TUNING, "curr_win_start=%d\n", curr_win_start);
	sdhc_dbg(AMLSD_DBG_TUNING, "curr_win_size=%d\n", curr_win_size);
	sdhc_dbg(AMLSD_DBG_TUNING, "best_win_start=%d\n", best_win_start);
	sdhc_dbg(AMLSD_DBG_TUNING, "best_win_size=%d\n", best_win_size);
	sdhc_dbg(AMLSD_DBG_TUNING, "wrap_win_start=%d\n", wrap_win_start);
	sdhc_dbg(AMLSD_DBG_TUNING, "wrap_win_size=%d\n", wrap_win_size);

	kfree(blk_test);

    // writel(vclk2_bak, host->base + SDHC_CLK2);
    // pdata->clk2 = vclk2_bak;
    // sdhc_err("vclk2_bak=%#x\n", vclk2_bak);
	
	//do not dynamical tuning for eMMC
    if ((pdata->is_in)&&!aml_card_type_mmc(pdata)) {
        schedule_delayed_work(&pdata->retuning, 15*HZ);
    }

	return ret;
}

static int aml_sdhc_execute_tuning (struct mmc_host *mmc, u32 opcode)
{
	// struct amlsd_platform *pdata = mmc_priv(mmc);
	// struct amlsd_host *host = pdata->host;
	struct aml_tuning_data tuning_data;
	int err = -ENOSYS;

    // if ((pdata->port != PORT_SDHC_A) && (pdata->port != PORT_SDHC_C)) { // debug
        // return 0;
    // }

    // if (pdata->is_tuned) { // have been tuned
        // writel(pdata->tune_phase, host->base + SDHC_CLK2);
        // pdata->clk2 = pdata->tune_phase;

        // printk("Tuning already, tune_phase=0x%x \n", pdata->tune_phase);
        // return 0;
    // }

    if (opcode == MMC_SEND_TUNING_BLOCK_HS200) {
        if (mmc->ios.bus_width == MMC_BUS_WIDTH_8) {
            tuning_data.blk_pattern = tuning_blk_pattern_8bit;
            tuning_data.blksz = sizeof(tuning_blk_pattern_8bit);
        } else if (mmc->ios.bus_width == MMC_BUS_WIDTH_4) {
            tuning_data.blk_pattern = tuning_blk_pattern_4bit;
            tuning_data.blksz = sizeof(tuning_blk_pattern_4bit);
        } else {
            return -EINVAL;
        }
    } else if (opcode == MMC_SEND_TUNING_BLOCK) {
		tuning_data.blk_pattern = tuning_blk_pattern_4bit;
		tuning_data.blksz = sizeof(tuning_blk_pattern_4bit);
	} else {
        sdhc_err("Undefined command(%d) for tuning\n", opcode);
		return -EINVAL;
	}

    err = aml_sdhc_execute_tuning_(mmc, opcode, &tuning_data);
	return err;
}

/*soft reset after errors*/
void aml_sdhc_host_reset(struct amlsd_host* host)
{
    writel(SDHC_SRST_ALL, host->base+SDHC_SRST);
    udelay(5);

    writel(0, host->base+SDHC_SRST);
    udelay(10);
}

/*setup reg initial value*/
static void aml_sdhc_reg_init(struct amlsd_host* host)
{
    struct sdhc_ctrl ctrl = {0};
    // struct sdhc_pdma pdma = {0};
    u32 vpdma = readl(host->base+SDHC_PDMA);
    struct sdhc_pdma* pdma = (struct sdhc_pdma*)&vpdma;
    struct sdhc_misc misc = {0};
    u32 vclkc;
    struct sdhc_clkc* clkc = (struct sdhc_clkc*)&vclkc;
    // struct sdhc_clk2 clk2 = {0};
    u32 venhc;
    struct sdhc_enhc *enhc = (struct sdhc_enhc *)&venhc;
    // u32 val;
#ifdef DMC_URGENT_PERIPH    
    u32 dmc_ctl;
#endif
    switch_mod_gate_by_type(MOD_SDHC, 1);
    // print_dbg("HHI_GCLK_MPEG0=%#x\n", READ_CBUS_REG(HHI_GCLK_MPEG0));

    aml_sdhc_host_reset(host);
    
#ifdef DMC_URGENT_PERIPH
#if (MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8)
    if(IS_MESON_M8_CPU()){
	    dmc_ctl = readl(P_MMC_QOS11_CTRL0);
	    dmc_ctl |= (1<<26);
	    writel(dmc_ctl, P_MMC_QOS11_CTRL0);
    }
    else{    
	    dmc_ctl = readl(P_DMC_AM7_CHAN_CTRL);
	    dmc_ctl |= (1<<18);
	    writel(dmc_ctl, P_DMC_AM7_CHAN_CTRL);
    }
#endif
#endif

    ctrl.rx_period = 0xf;//0x08; // 0xf;
    ctrl.rx_timeout = 0x7f;//0x40; // 0x7f;

    /*R/W endian 7*/
    ctrl.rx_endian = 0x7;
    ctrl.tx_endian = 0x7;
    writel(*(u32*)&ctrl, host->base + SDHC_CTRL);

    vclkc = readl(host->base+SDHC_CLKC);
    clkc->mem_pwr_off = 0;
    writel(vclkc, host->base+SDHC_CLKC);

    pdma->dma_mode = 0;
    pdma->dma_urgent = 1;

    pdma->wr_burst = 7;//3; // means 4
    pdma->txfifo_th = 49; // means 49
    pdma->rd_burst = 15; // means 8
    pdma->rxfifo_th = 7; // means 8
    //pdma->rxfifo_manual_flush = 1; // bit[29]=1, bit[30]=0
    // pdma->rd_burst = 3;
    // pdma->wr_burst = 3;
    // pdma->rxfifo_th = 7;
    // pdma->txfifo_th = 0x18;
    writel(vpdma, host->base+SDHC_PDMA);

    /*Send Stop Cmd automatically*/
    if(IS_MESON_M8M2_CPU)
         misc.txstart_thres = 6;//4; // [29:31] = 7
    else
         misc.txstart_thres = 7; // [29:31] = 7
    misc.manual_stop = 0;
    misc.wcrc_err_patt = 5;
    misc.wcrc_ok_patt = 2;
    writel(*(u32*)&misc, host->base + SDHC_MISC);

    venhc = readl(host->base+SDHC_ENHC);
    
    if(IS_MESON_M8M2_CPU){
	    enhc->reg.meson8m2.rxfifo_th = 64;
	    enhc->reg.meson8m2.sdio_irq_period = 12;
	    enhc->reg.meson8m2.debug = 1;
	    enhc->reg.meson8m2.chk_dma = 0;
	    enhc->reg.meson8m2.chk_wrrsp = 0;
	    enhc->reg.meson8m2.wrrsp_mode = 1;
    }
   else{
	    enhc->reg.meson.rxfifo_th = 63;
	    enhc->reg.meson.dma_rd_resp = 0;
	    enhc->reg.meson.dma_wr_resp = 1;
	    enhc->reg.meson.sdio_irq_period = 12;
	    enhc->reg.meson.rx_timeout = 255;
    }
    writel(venhc, host->base + SDHC_ENHC);

    /*Disable All Irq*/
    writel(0, host->base + SDHC_ICTL);

    /*Write 1 Clear all irq status*/
    writel(SDHC_ISTA_W1C_ALL, host->base + SDHC_ISTA);
}

/*wait sdhc controller cmd send*/
int aml_sdhc_wait_ready(struct amlsd_host* host, u32 timeout)
{
    u32 i, vstat=0;
    struct sdhc_stat* stat;
    u32 esta;

    for(i=0; i< timeout; i++){
        vstat = readl(host->base + SDHC_STAT);
        stat = (struct sdhc_stat*)&vstat;

        esta = readl(host->base + SDHC_ESTA);
        if(!stat->cmd_busy && (!((esta >> 11) & 7)))
        //if(!stat->cmd_busy)
            return 0;
        udelay(1);
    }

    sdhc_err("SDHC_STAT=%#x, sdhc controller is busy.\n", vstat);
    //aml_sdhc_print_reg(host);
    aml_sdhc_host_reset(host);
    //WARN_ON(1);
    return -1;
}

/*
read response from REG0_ARGU(136bit or 48bit)
*/
int aml_sdhc_read_response(struct mmc_host *mmc, struct mmc_command *cmd)
{
    u32 i=0, j=0;
    struct amlsd_platform* pdata = mmc_priv(mmc);
    struct amlsd_host* host = pdata->host;
    u32 vpdma = readl(host->base+SDHC_PDMA);
    struct sdhc_pdma* pdma = (struct sdhc_pdma*)&vpdma;
    u32* presp = (u32*)cmd->resp;

    pdma->dma_mode = 0;
    if(cmd->flags & MMC_RSP_136) /*136 bit*/{
        for(i=MMC_RSP_136_NUM,j=0; i>=1; i--,j++){
            pdma->pio_rdresp = i;
            writel(vpdma ,host->base+SDHC_PDMA);
            presp[j] = readl(host->base+SDHC_ARGU);
            sdhc_dbg(AMLSD_DBG_RESP,"Cmd %d ,Resp[%d] 0x%x\n",
                    cmd->opcode, j, presp[j]);
        }
    }
    else/*48 bit*/{
        pdma->pio_rdresp = i;
        writel(vpdma ,host->base+SDHC_PDMA);
        presp[0] = readl(host->base+SDHC_ARGU);
        sdhc_dbg(AMLSD_DBG_RESP,"Cmd %d ,Resp 0x%x\n", cmd->opcode, presp[0]);
    }
    return 0;
}

/*clear fifo after transfer data*/
void aml_sdhc_clear_fifo(struct amlsd_host* host)
{
    struct sdhc_srst srst;

    srst.rxfifo = 1;
    srst.txfifo = 1;
    writel(*(u32*)&srst, host->base+SDHC_SRST);
    udelay(1);
}

/*enable irq bit in reg SDHC_ICTL*/
inline void aml_sdhc_enable_imask(struct amlsd_host* host, u32 irq)
{
    u32 ictl = readl(host->base+SDHC_ICTL);
    ictl |= irq;
    writel(ictl, host->base+SDHC_ICTL);
}

/*disable irq bit in reg SDHC_ICTL*/
inline void aml_sdhc_disable_imask(struct amlsd_host* host, u32 irq)
{
    u32 ictl = readl(host->base+SDHC_ICTL);
    ictl &= ~irq;
    writel(ictl, host->base+SDHC_ICTL);
}

void aml_sdhc_set_pdma(struct amlsd_platform* pdata, struct mmc_request* mrq)
{
    struct amlsd_host* host = pdata->host;
    u32 vpdma = readl(host->base+SDHC_PDMA);
    struct sdhc_pdma* pdma = (struct sdhc_pdma*)&vpdma;
//    u32 venhc = readl(host->base+SDHC_ENHC);
    //struct sdhc_enhc *enhc = (struct sdhc_enhc *)&venhc;

    BUG_ON(!mrq->data);
#if 1
    pdma->dma_mode = 1;
    if(IS_MESON_M8M2_CPU){
	    writel(*(u32*)pdma, host->base+SDHC_PDMA);
	    return;
    }
    if(mrq->data->flags & MMC_DATA_WRITE){
        /*self-clear-fill, recommend to write before sd send*/
        //init sets rd_burst to 15
        pdma->rd_burst = 31;
        pdma->txfifo_fill = 1;
        writel(*(u32*)pdma, host->base+SDHC_PDMA);
        pdma->txfifo_fill = 1;
    }
	else{
	    if(aml_card_type_sdio(pdata)){
	        pdma->rxfifo_manual_flush = 0; 
	        //enhc->dma_wr_resp = 1;
	    }
	    else{
	        pdma->rxfifo_manual_flush = 1; 
	        //enhc->dma_wr_resp = 0;
	    }
#if 0	    
		if(mrq->data->blocks > 1){
		    pdma->wr_burst = 7;
		}else{
		    pdma->wr_burst = 3;
		}
#endif		
		//writel(venhc, host->base + SDHC_ENHC);
	}

#else
    if(mrq->data->flags & MMC_DATA_WRITE){
        /*self-clear-fill, recommend to write before sd send*/
        if(mrq->data->blocks > 1){
            pdma->txfifo_fill = 1;

            pdma->rd_burst = 7;
            pdma->wr_burst = 7;
            pdma->rxfifo_th = 7;
            pdma->txfifo_th = 32;
        }else{
            pdma->rd_burst = 3;
            pdma->wr_burst = 3;
            pdma->rxfifo_th = 0x7;
            pdma->txfifo_th = 0x7;
        }
    }else{
        //pdma->rxfifo_manual_flush = 2;
        if(mrq->data->blocks > 1){
            pdma->rd_burst = 7;
            pdma->wr_burst = 7;
            pdma->rxfifo_th = 32;
            pdma->txfifo_th = 7;
        }else{
            pdma->rd_burst = 3;
            pdma->wr_burst = 3;
            pdma->rxfifo_th = 0x7;
            pdma->txfifo_th = 0x7;
        }
    }
#endif
    
    writel(*(u32*)pdma, host->base+SDHC_PDMA);
    if(mrq->data->flags & MMC_DATA_WRITE){
        //init sets rd_burst to 15
        //change back to 15 after fill
        pdma->rd_burst = 15;
        writel(*(u32*)pdma, host->base+SDHC_PDMA);
    }
}

/*copy buffer from data->sg to dma buffer, set dma addr to reg*/
void aml_sdhc_prepare_dma(struct amlsd_host *host, struct mmc_request *mrq)
{
    struct mmc_data *data = mrq->data;
    
#if 1 //for temp write test
    if(data->flags & MMC_DATA_WRITE){
        aml_sg_copy_buffer(data->sg, data->sg_len,
                host->bn_buf, data->blksz*data->blocks, 1);
        sdhc_dbg(AMLSD_DBG_WR_DATA,"W Cmd %d, %x-%x-%x-%x\n",
                mrq->cmd->opcode,
                host->bn_buf[0], host->bn_buf[1],
                host->bn_buf[2], host->bn_buf[3]);
    }
 #endif
}

/*
* set host->clkc_w for 8bit emmc write cmd as it would fail on TXFIFO EMPTY,
* we decrease the clock for write cmd, and set host->clkc for other cmd
*/
void aml_sdhc_set_clkc(struct amlsd_platform* pdata)
{
    struct amlsd_host* host = pdata->host;
    u32 vclkc = readl(host->base + SDHC_CLKC);
    u32 clk2 = readl(host->base+SDHC_CLK2);

    if (!host->is_gated && (pdata->clkc == vclkc) && (pdata->clk2 == clk2))
        return;

    if (host->is_gated) { // if clock is switch off, we need turn on
        struct sdhc_clkc* clkc = (struct sdhc_clkc*)&(pdata->clkc);
        aml_sdhc_clk_switch(pdata, clkc->clk_div, clkc->clk_src_sel);
    } else {
        writel(pdata->clkc, host->base+SDHC_CLKC);
    }

    writel(pdata->clk2, host->base+SDHC_CLK2);
    
    // sdhc_dbg(AMLSD_DBG_CLKC, "[aml_sdhc_set_clkc]vclkc %x pdata->clkc %x,"
            // "clk_div %d\n", vclkc, pdata->clkc, clkc->clk_div);
}

void aml_sdhc_start_cmd(struct amlsd_platform* pdata, struct mmc_request* mrq)
{
    struct amlsd_host* host = pdata->host;
    struct sdhc_send send = {0};
    struct sdhc_ictl ictl = {0};
    u32 vctrl = readl(host->base + SDHC_CTRL);
    struct sdhc_ctrl* ctrl = (struct sdhc_ctrl*)&vctrl;
    u32 vstat;
    struct sdhc_stat* stat = (struct sdhc_stat*)&vstat;
    u32 vsrst;
    struct sdhc_srst *srst = (struct sdhc_srst *)&vsrst;
    u32 vmisc = readl(host->base + SDHC_MISC);
    struct sdhc_misc* misc = (struct sdhc_misc*)&vmisc;               
    int i;
    int loop_limit;
    u32 vesta;


    /*Set clock for each port, change clock before wait ready*/
    aml_sdhc_set_clkc(pdata);

    /*Set Irq Control*/
    ictl.data_timeout = 1;
    ictl.data_err_crc = 1;
    ictl.rxfifo_full = 1;
    ictl.txfifo_empty = 1;
    ictl.resp_timeout = 1; // try
    ictl.resp_err_crc = 1; // try

    /*Response with busy*/
    //if((mrq->cmd->flags & MMC_RSP_R1B)== MMC_RSP_R1B){
        //send.r1b = 1;
        //sdhc_err("cmd %d set r1b\n", mrq->cmd->opcode);
    //}

    if(mrq->data){
        if(((mrq->cmd->opcode == SD_IO_RW_DIRECT) || (mrq->cmd->opcode == SD_IO_RW_EXTENDED))
                && (mrq->data->blocks > 1)){
            misc->manual_stop = 1;
        }
        else{
            misc->manual_stop = 0;
        }
        writel(vmisc, host->base + SDHC_MISC);
        
        vstat = readl(host->base + SDHC_STAT);
        if(stat->txfifo_cnt || stat->rxfifo_cnt){
            // sdhc_err("cmd%d: txfifo_cnt:%d, rxfifo_cnt:%d\n", 
                // mrq->cmd->opcode, stat->txfifo_cnt, stat->rxfifo_cnt);

            if(aml_sdhc_wait_ready(host, STAT_POLL_TIMEOUT)){ /*Wait command busy*/
            	sdhc_err("aml_sdhc_wait_ready error before start cmd fifo\n");
            }
            vsrst = readl(host->base + SDHC_SRST);        
            srst->rxfifo = 1;
            srst->txfifo = 1;
            srst->main_ctrl = 1;
            writel(vsrst, host->base+SDHC_SRST);
            udelay(5);
            writel(vsrst, host->base+SDHC_SRST);
        }
        vstat = readl(host->base + SDHC_STAT);
        if(stat->txfifo_cnt || stat->rxfifo_cnt){
            sdhc_err("FAIL to clear FIFO, cmd%d: txfifo_cnt:%d, rxfifo_cnt:%d\n", 
                mrq->cmd->opcode, stat->txfifo_cnt, stat->rxfifo_cnt);
        }

        /*Command has data*/
        send.cmd_has_data = 1;

        /*Read/Write data direction*/
        if(mrq->data->flags & MMC_DATA_WRITE)
            send.data_dir = 1;


        /*Set package size*/
        if(mrq->data->blksz < 512)
            ctrl->pack_len = mrq->data->blksz;
        else
            ctrl->pack_len = 0;

        /*Set blocks in package*/
        send.total_pack = mrq->data->blocks - 1;

        /*
         * If command with no data, just wait response done
         * interrupt(int[0]), and if command with data transfer, just
         * wait dma done interrupt(int[11]), don't need care about
         * dat0 busy or not.
         */
        if(IS_MESON_M8M2_CPU)
        	ictl.dma_done = 1; // for hardware automatical flush
	 else{         
	        if((mrq->data->flags & MMC_DATA_WRITE) 
	            || aml_card_type_sdio(pdata))
	            ictl.dma_done = 1; // for hardware automatical flush
	        else
	            ictl.data_xfer_ok = 1; // for software flush
	 }            
    }else
        ictl.resp_ok = 1;

    if (mrq->cmd->opcode == MMC_STOP_TRANSMISSION) {
        send.data_stop = 1;
    }


    /*Set Bus Width*/
    ctrl->dat_type = pdata->width;

    if(mrq->cmd->flags & MMC_RSP_136)
        send.resp_len = 1;
    if(mrq->cmd->flags & MMC_RSP_PRESENT)
        send.cmd_has_resp = 1;
    if(!(mrq->cmd->flags & MMC_RSP_CRC)|| mrq->cmd->flags & MMC_RSP_136)
        send.resp_no_crc = 1;

    /*Command Index*/
    send.cmd_index = mrq->cmd->opcode;

    writel(*(u32*)&ictl, host->base+SDHC_ICTL);

    /*Set irq status: write 1 clear*/
    writel(SDHC_ISTA_W1C_ALL, host->base+SDHC_ISTA);

    writel(mrq->cmd->arg, host->base+SDHC_ARGU);
    writel(vctrl, host->base+SDHC_CTRL);
    writel(host->bn_dma_buf, host->base+SDHC_ADDR);
    if(aml_sdhc_wait_ready(host, STAT_POLL_TIMEOUT)){ /*Wait command busy*/
		sdhc_err("aml_sdhc_wait_ready error before start cmd\n");
	}
    if(mrq->data)
        aml_sdhc_set_pdma(pdata, mrq);/*Start dma transfer*/

#ifdef      CONFIG_MMC_AML_DEBUG
    if (0) {
        memcpy_fromio(host->reg_buf, host->base, 0x3C);
        host->reg_buf[SDHC_SEND/4] = *(u32*)&send; 
    }
#endif

    if(!IS_MESON_M8M2_CPU){
	    loop_limit = 100;
	    for (i = 0; i < loop_limit; i++) {
	        vesta = readl(host->base + SDHC_ESTA);
	        if (vesta == 0) {
	            // sdhc_err("ok: %s: cmd%d, SDHC_ESTA=%#x, i=%d\n", 
	                    // mmc_hostname(host->mmc), mrq->cmd->opcode, vesta, i);
	            break;
	        }
	        if (i > 50) {
	            sdhc_err("udelay\n");
	            udelay(1);
	        }
	    }
	    if (i >= loop_limit) {
	        sdhc_err("Warning: %s: cmd%d, SDHC_ESTA=%#x\n", 
	                mmc_hostname(host->mmc), mrq->cmd->opcode, vesta);
	    }

	    if (mrq->data && (mrq->data->flags & MMC_DATA_WRITE)) {
	        for (i = 0; i < loop_limit; i++) {
	            vstat = readl(host->base + SDHC_STAT);
	            if (stat->txfifo_cnt != 0) {
	                // sdhc_err("OK: %s: cmd%d, txfifo_cnt=%d, i=%d\n", 
	                        // mmc_hostname(host->mmc), mrq->cmd->opcode, stat->txfifo_cnt, i);
	                break;
	            }
	            udelay(1);
	        }
	        if (i >= loop_limit) {
	            sdhc_err("Warning: %s: cmd%d, txfifo_cnt=%d\n", 
	                    mmc_hostname(host->mmc), mrq->cmd->opcode, stat->txfifo_cnt);
	        }

	    }
    }

    writel(*(u32*)&send, host->base+SDHC_SEND); /*Command send*/
}

/*mmc_request_done & do nothing in xfer_post*/
void aml_sdhc_request_done(struct mmc_host *mmc, struct mmc_request *mrq)
{
    struct amlsd_platform * pdata = mmc_priv(mmc);
    struct amlsd_host* host = pdata->host;
    unsigned long flags;

    spin_lock_irqsave(&host->mrq_lock, flags);
    host->xfer_step = XFER_FINISHED;
    host->mrq = NULL;
    host->status = HOST_INVALID;
    spin_unlock_irqrestore(&host->mrq_lock, flags);

#ifdef      CONFIG_MMC_AML_DEBUG
    host->req_cnt--;

    aml_dbg_verify_pinmux(pdata);
    aml_dbg_verify_pull_up(pdata);

    if (0) {
        sdhc_err("%s: cmd%d, start sdhc reg:\n", mmc_hostname(host->mmc), mrq->cmd->opcode);
        aml_sdhc_print_reg_(host->reg_buf);
        sdhc_err("done reg:\n");
        aml_sdhc_print_reg(host);
    }
#endif

    if (pdata->xfer_post)
        pdata->xfer_post(pdata);

    aml_sdhc_disable_imask(host, SDHC_ICTL_ALL);
    /*Set irq status: write 1 clear*/
    writel(SDHC_ISTA_W1C_ALL, host->base+SDHC_ISTA);
    //aml_sdhc_wait_ready(host, STAT_POLL_TIMEOUT);
    if(aml_sdhc_wait_ready(host, STAT_POLL_TIMEOUT)){ /*Wait command busy*/
		sdhc_err("aml_sdhc_wait_ready request done\n");
    }
    mmc_request_done(host->mmc, mrq);
}

char *msg_err[] = {
    "invalid",          /* 0 */
    "rxfifo full",      /* 1 */
    "txfifo empty",     /* 2 */
    "rsp CRC",          /* 3 */
    "data CRC",         /* 4 */
    "rsp timeout",      /* 5 */
    "data timeout",     /* 6 */
};
static void aml_sdhc_print_err (struct amlsd_host *host)
{
    char *msg, *msg_timer="";
    char *p;
    u32 tmp_reg, xfer_step, xfer_step_prev, status;
    int left_size;
    struct clk* clk_src = clk_get_sys("clk81", NULL);
    int clk18_clk_rate = clk_get_rate(clk_src);
    struct amlsd_platform * pdata = mmc_priv(host->mmc);
    u32 vista = readl(host->base + SDHC_ISTA);
    u32 vstat = readl(host->base + SDHC_STAT);
    u32 vclkc = readl(host->base+SDHC_CLKC);
    struct sdhc_clkc* clkc = (struct sdhc_clkc*)&vclkc;
    u32 vctrl = readl(host->base + SDHC_CTRL);
    struct sdhc_ctrl* ctrl = (struct sdhc_ctrl*)&vctrl;
    u32 clk_rate;
    unsigned long flags;

    if ((host->mrq->cmd->opcode == MMC_SEND_TUNING_BLOCK) 
            || (host->mrq->cmd->opcode == MMC_SEND_TUNING_BLOCK_HS200)) { // not print err msg for tuning cmd
        return ;
    }

    spin_lock_irqsave(&host->mrq_lock, flags);
    xfer_step = host->xfer_step;
    xfer_step_prev = host->xfer_step_prev;
    status = host->status;
    spin_unlock_irqrestore(&host->mrq_lock, flags);

    clk_src = clk_get_sys("pll_fixed", NULL); // fclk
    clk_rate = clk_get_rate(clk_src)/3; // for SDHC_CLOCK_SRC_FCLK_DIV3

    p = host->msg_buf;
    left_size = MESSAGE_BUF_SIZE;

    if (((status < HOST_ERR_END) && (status > HOST_INVALID))
            && (status < ARRAY_SIZE(msg_err))) { // valid sdhc errors
        msg = msg_err[status];
    } else {
        msg = "status is invalid";
    }

    if (xfer_step == XFER_TIMER_TIMEOUT) { // by aml_sdhc_timeout()
        if (((status < HOST_ERR_END) && (status > HOST_INVALID))
                && (status < ARRAY_SIZE(msg_err))) { // valid sdhc errors
            msg_timer = "timer timeout WITH sdhc ";
        } else {
            msg_timer = "timer timeout";
            msg = "";
        }
    }

    aml_snprint(&p, &left_size, "%s: %s%s error, port=%d, Cmd%d Arg %08x, xfer_step=%d, status=%d, cmd25=%d, fifo_empty=%d, fifo_full=%d, timeout=%d, ",
            mmc_hostname(host->mmc), 
            msg_timer,
            msg,
            pdata->port,
            host->mrq->cmd->opcode,
            host->mrq->cmd->arg,
            xfer_step,
            status,
            cmd25_cnt,
            fifo_empty_cnt,
            fifo_full_cnt,
            timeout_cnt);

    switch (status) // more to print for different error cases
    {
        case HOST_RX_FIFO_FULL:
        case HOST_TX_FIFO_EMPTY:
            aml_snprint(&p, &left_size, "clk81=%d, clock=%d, ", 
                    clk18_clk_rate, clk_rate/(clkc->clk_div+1));
            break;
    }

    if (xfer_step == XFER_TIMER_TIMEOUT) { // by aml_sdhc_timeout()
        aml_snprint(&p, &left_size, "xfer_step_prev=%d, ", xfer_step_prev);
    }    

    if (host->mrq->data) {
        int byte = host->mrq->data->blksz*host->mrq->data->blocks;

        aml_snprint(&p, &left_size, "Xfer %d Bytes, ", byte);
        if (byte > 512) // multi-block mode
            aml_snprint(&p, &left_size, "SEND=%#x, ", readl(host->base+SDHC_SEND));

        if (pdata->width != ctrl->dat_type) { // error: bus width is different
            aml_snprint(&p, &left_size, "pdata->width=%d, ctrl->dat_type=%d, ",
                    pdata->width, ctrl->dat_type);
        }

        if (pdata->clkc != vclkc) { // error: clock setting is different
            aml_snprint(&p, &left_size, "pdata->clkc=%d, vclkc=%#x, ",
                    pdata->clkc, vclkc);
        }
    }
    aml_snprint(&p, &left_size, "iSTA=%#x, STAT=%#x", vista, vstat);

    sdhc_err("%s\n", host->msg_buf);

    // tmp_reg = readl((u32 *)P_AO_SECURE_REG1);
    // if (tmp_reg) {
        // sdhc_err("P_AO_SECURE_REG1=%#x\n", tmp_reg);
    // }
    tmp_reg = READ_CBUS_REG(HHI_GCLK_MPEG0);
    if (!(tmp_reg & 0x00004000)) {
        sdhc_err("Error: SDHC is gated clock, HHI_GCLK_MPEG0=%#x, bit14 is 0\n", tmp_reg);
    }
    aml_dbg_print_pinmux();

#ifdef      CONFIG_MMC_AML_DEBUG
    if (xfer_step == XFER_TIMER_TIMEOUT) { // by aml_sdhc_timeout()
        sdhc_err("old sdhc reg:\n");
        aml_sdhc_print_reg_(host->reg_buf);
    }    
#endif
    aml_sdhc_print_reg(host);
}

/*error handler*/
static void aml_sdhc_timeout(struct work_struct *work)
{
    static int timeout_cnt = 0;
    struct amlsd_host *host = container_of(work, struct amlsd_host, timeout.work);
    // u32 vista = readl(host->base + SDHC_ISTA);
    // struct sdhc_ista* ista = (struct sdhc_ista*)&vista;
    struct mmc_request* mrq;
    struct amlsd_platform* pdata = mmc_priv(host->mmc);
    
    unsigned long flags;

//	struct timeval ts_current;
	unsigned long time_start_cnt = READ_CBUS_REG(ISA_TIMERE);
	
    time_start_cnt = (time_start_cnt - host->time_req_sta) / 1000;
	
    BUG_ON(!host->mrq || !host->mrq->cmd);

    spin_lock_irqsave(&host->mrq_lock, flags);
    if(host->xfer_step == XFER_FINISHED){
        spin_unlock_irqrestore(&host->mrq_lock, flags);
        sdhc_err("timeout after xfer finished\n");
        return;
    }

    if ((host->xfer_step == XFER_IRQ_TASKLET_DATA) 
            || (host->xfer_step == XFER_IRQ_TASKLET_CMD)) {
        schedule_delayed_work(&host->timeout, 50);
        host->time_req_sta = READ_CBUS_REG(ISA_TIMERE);
        
        timeout_cnt++;
        if (timeout_cnt > 30)
            goto timeout_handle;

        spin_unlock_irqrestore(&host->mrq_lock, flags);

        sdhc_err("%s: cmd%d, ISR have been run, xfer_step=%d, time_start_cnt=%ldmS, timeout_cnt=%d\n",
                mmc_hostname(host->mmc), host->mrq->cmd->opcode, host->xfer_step, time_start_cnt, timeout_cnt);
        return;
    }
timeout_handle:
    timeout_cnt = 0;

    mrq = host->mrq;
    host->xfer_step_prev = host->xfer_step;
    host->xfer_step = XFER_TIMER_TIMEOUT;
    mrq->cmd->error = -ETIMEDOUT;

    //do not retry for sdcard & sdio wifi
    if(!aml_card_type_mmc(pdata)){
        sdhc_error_flag = 0;
        mrq->cmd->retries = 0;         
    }
    else if(((sdhc_error_flag & (1<<3)) == 0)  && (mrq->data != NULL) 
            && pdata->is_in){  //set cmd retry cnt when first error.
        sdhc_error_flag |= (1<<3);
        mrq->cmd->retries = AML_TIMEOUT_RETRY_COUNTER; 
    }   

    if(sdhc_error_flag && (mrq->cmd->retries == 0)){
        sdhc_error_flag |= (1<<30);
        sdhc_err("Command retried failed\n");
    }
        
    //spin_unlock_irqrestore(&host->mrq_lock, flags);

    aml_sdhc_status(host);

    spin_unlock_irqrestore(&host->mrq_lock, flags);
    aml_sdhc_read_response(host->mmc, mrq->cmd);
    sdhc_err("time_start_cnt:%ld\n", time_start_cnt);

    aml_sdhc_print_err(host);

    /*read response, if error, set -EILSEQ, then retry in aml_sdhc_request_done*/
    // if(R1_STATUS(host->mrq->cmd->resp[0]) & R1_COM_CRC_ERROR ){
        // mrq->cmd->error = -EILSEQ;
        // sdhc_err("Cmd CRC error\n");
        // vista = readl(host->base+SDHC_ISTA);
        // sdhc_err("Cmd %d Resp 0x%x Xfer %d\n", host->mrq->cmd->opcode,
                // host->mrq->cmd->resp[0],
                // mrq->data?mrq->data->blksz*mrq->data->blocks:0);
        // goto req_done;
    // }


    /*
        * response crc error & data crc error with xfer size >= 512,
        * ignore crc error with data transfer less then 512, for example 64B
        */
    // if((ista->resp_err_crc && !mrq->data)||
        // (ista->data_err_crc && mrq->data && mrq->data->blksz == 512)){
        // mrq->cmd->error = -EILSEQ;
        // sdhc_err("CRC error Cmd %d Ista 0x%x\n", mrq->cmd->opcode, vista);
        // goto req_done;
    // }

    // [>get TXFIFO_EMPTY as a CRC error <]
    // if(ista->txfifo_empty){
        // mrq->cmd->error = -EILSEQ;
        // sdhc_err("TXFIFO_EMPTY Cmd %d Size 0x%x\n",
                // mrq->cmd->opcode, mrq->data->blksz*mrq->data->blocks);
        // goto req_done;
    // }
    // [>get RXFIFO_FULL as a CRC error <]
    // if(ista->rxfifo_full){
        // mrq->cmd->error = -EILSEQ;
        // sdhc_err("RXFIFO_FULL Cmd %d Size 0x%x\n",
                // mrq->cmd->opcode, mrq->data->blksz*mrq->data->blocks);
        // goto req_done;
    // }

    // if(ista->resp_timeout){
        // sdhc_err("TIMEOUT Cmd %d Ista 0x%x\n", mrq->cmd->opcode, vista);
        // mrq->cmd->error = -ETIMEDOUT;
        // goto req_done;
    // }

    // sdhc_err("Cmd %d, Ista 0x%x Request done\n", mrq->cmd->opcode, vista);
    // mrq->cmd->error = -1;

// req_done:
    aml_sdhc_host_reset(host);
    /*write 1 clear irq status reg*/
    //writel(vista, host->base+SDHC_ISTA);
 
    //do not send stop for sdio wifi case 
    if(host->mrq->stop && aml_card_type_mmc(pdata) && !host->cmd_is_stop
        && (host->mrq->cmd->opcode != MMC_SEND_TUNING_BLOCK) 
                    && (host->mrq->cmd->opcode != MMC_SEND_TUNING_BLOCK_HS200)){
    //if((host->mrq->cmd->opcode != SD_IO_RW_DIRECT) && (host->mrq->cmd->opcode != SD_IO_RW_EXTENDED) 
   //         && (!mmc_card_removed(pdata->mmc->card)) && (!mrq->data)){
        //spin_lock_irqsave(&host->mrq_lock, flags);
        aml_sdhc_send_stop(host);                
        //spin_unlock_irqrestore(&host->mrq_lock, flags);
        //schedule_delayed_work(&host->timeout, 50);
    }
    else{
        spin_lock_irqsave(&host->mrq_lock, flags);        
        if (host->cmd_is_stop)
            host->cmd_is_stop = 0;
        spin_unlock_irqrestore(&host->mrq_lock, flags);

        aml_sdhc_request_done(host->mmc, mrq);
    }          
      
    return ;
}

static void aml_sdhc_tuning_timer(struct work_struct *work)
{
    struct amlsd_platform * pdata = container_of(work, struct amlsd_platform, retuning.work);
    struct amlsd_host *host = (void*)pdata->host;
    unsigned long flags;

    spin_lock_irqsave(&host->mrq_lock, flags);
    pdata->need_retuning = true;
    spin_unlock_irqrestore(&host->mrq_lock, flags);
}

/*cmd request interface*/
void aml_sdhc_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
    struct amlsd_platform * pdata;
    struct amlsd_host *host;
    // u32 vista;
    unsigned long flags;
    unsigned int timeout;
    u32 tuning_opcode;
    
    BUG_ON(!mmc);
    BUG_ON(!mrq);

    pdata = mmc_priv(mmc);
    host = (void*)pdata->host;

    if (aml_check_unsupport_cmd(mmc, mrq))
        return;

    //only for SDCARD
    if(!pdata->is_in || (!host->init_flag && aml_card_type_sd(pdata))){
        spin_lock_irqsave(&host->mrq_lock, flags);
        mrq->cmd->error = -ENOMEDIUM;
        mrq->cmd->retries = 0;
        spin_unlock_irqrestore(&host->mrq_lock, flags);
        mmc_request_done(mmc, mrq);
        return;
    }

    if (pdata->need_retuning && mmc->card) {
        /* eMMC uses cmd21 but sd and sdio use cmd19 */
        tuning_opcode = (mmc->card->type == MMC_TYPE_MMC)?
            MMC_SEND_TUNING_BLOCK_HS200 : MMC_SEND_TUNING_BLOCK;
        aml_sdhc_execute_tuning(mmc, tuning_opcode);
    }

    // aml_sdhc_host_reset(host);
    // vista = readl(host->base+SDHC_ISTA);
    // writel(vista, host->base+SDHC_ISTA);
    aml_sdhc_disable_imask(host, SDHC_ICTL_ALL);

    sdhc_dbg(AMLSD_DBG_REQ ,"%s: starting CMD%u arg %08x flags %08x\n",
        mmc_hostname(mmc), mrq->cmd->opcode,
        mrq->cmd->arg, mrq->cmd->flags);

#ifdef      CONFIG_AML_MMC_DEBUG_FORCE_SINGLE_BLOCK_RW
    if ((mrq->cmd->opcode == 18) || (mrq->cmd->opcode == 25)) { // ???? for debug
        sdhc_err("cmd%d\n", mrq->cmd->opcode);
    }
#endif

    if (mrq->cmd->opcode == 0) {
        cmd25_cnt = 0;
        fifo_empty_cnt = 0;
        fifo_full_cnt = 0;
        timeout_cnt = 0;
        host->init_flag = 1;
    }
    
    if (mrq->cmd->opcode == 25)
        cmd25_cnt++;
       
    //clear error flag if last command retried failed here    
    if(sdhc_error_flag & (1<<30)){
        sdhc_error_flag = 0;
    }

    /*setup reg  especially for cmd with transfering data*/
    if(mrq->data) {
        /*Copy data to dma buffer for write request*/
        aml_sdhc_prepare_dma(host, mrq);

        sdhc_dbg(AMLSD_DBG_REQ ,"%s: blksz %d blocks %d flags %08x "
            "tsac %d ms nsac %d\n",
            mmc_hostname(mmc), mrq->data->blksz,
            mrq->data->blocks, mrq->data->flags,
            mrq->data->timeout_ns / 1000000,
            mrq->data->timeout_clks);
    }

    /*clear pinmux & set pinmux*/
    if(pdata->xfer_pre)
        pdata->xfer_pre(pdata);

#ifdef      CONFIG_MMC_AML_DEBUG
    aml_dbg_verify_pull_up(pdata);
    aml_dbg_verify_pinmux(pdata);
#endif

    if(!mrq->data)
        timeout = 100; //mod_timer(&host->timeout_tlist, jiffies + 100); // 1s
    else
        timeout = 500;//mod_timer(&host->timeout_tlist,
                //jiffies + 500/*10*nsecs_to_jiffies(mrq->data->timeout_ns)*/); // 5s
             
    schedule_delayed_work(&host->timeout, timeout);

    spin_lock_irqsave(&host->mrq_lock, flags);
    if(host->xfer_step != XFER_FINISHED && host->xfer_step != XFER_INIT)
        sdhc_err("host->xfer_step %d\n", host->xfer_step);

    /*host->mrq, used in irq & tasklet*/
    host->mrq = mrq;
    host->mmc = mmc;
    host->xfer_step = XFER_START;
    host->opcode = mrq->cmd->opcode;
    host->arg = mrq->cmd->arg;
    host->time_req_sta = READ_CBUS_REG(ISA_TIMERE);

    /*setup reg for all cmd*/
    aml_sdhc_start_cmd(pdata, mrq);
    host->xfer_step = XFER_AFTER_START;
    spin_unlock_irqrestore(&host->mrq_lock, flags);
}
    
static int aml_sdhc_status (struct amlsd_host* host)
{
    int ret = -1; // error
    u32 victl = readl(host->base + SDHC_ICTL);
    u32 vista = readl(host->base + SDHC_ISTA);
    struct sdhc_ista* ista = (struct sdhc_ista*)&vista;
    struct mmc_request* mrq = host->mrq;
//    unsigned long flags;

    if(!mrq){
        sdhc_err("NULL mrq\n");
        return ret;
    }

    //spin_lock_irqsave(&host->mrq_lock, flags);
    if (victl & vista) {
        if(ista->rxfifo_full){
            host->status = HOST_RX_FIFO_FULL;
            goto _status_exit;
        }
        if(ista->txfifo_empty){
            host->status = HOST_TX_FIFO_EMPTY;
            goto _status_exit;
        }
        if (ista->resp_err_crc) {
            host->status = HOST_RSP_CRC_ERR;
            goto _status_exit;
        }
        if (ista->data_err_crc) {
            host->status = HOST_DAT_CRC_ERR;
            goto _status_exit;
        }
        if (ista->resp_timeout) {
            host->status = HOST_RSP_TIMEOUT_ERR;
            goto _status_exit;
        }
        if (ista->data_timeout) {
            host->status = HOST_DAT_TIMEOUT_ERR;
            goto _status_exit;
        }
        if(ista->dma_done){
            host->status = HOST_TASKLET_DATA;
            ret = 0; // ok
            goto _status_exit;
        }
        if(ista->data_xfer_ok){
            host->status = HOST_TASKLET_DATA;
            ret = 0; // ok
            goto _status_exit;
        }
        if(ista->resp_ok_noclear){
            host->status = HOST_TASKLET_CMD;
            ret = 0; // ok
            goto _status_exit;
        }
    }
    ret = 0; // ok
_status_exit:
    
    //for debug
    //sdhc_debug_status(host);

    //spin_unlock_irqrestore(&host->mrq_lock, flags);
    return ret;
}

/*sdhc controller irq*/
static irqreturn_t aml_sdhc_irq(int irq, void *dev_id)
{
    struct amlsd_host* host = dev_id;
    struct mmc_host* mmc;
    struct amlsd_platform* pdata;
    struct mmc_request* mrq;
    unsigned long flags;
    bool exception_flag = false;
    u32 victl ;//= readl(host->base + SDHC_ICTL);
    u32 vista ;//= readl(host->base + SDHC_ISTA);

    //if(sdhc_debug_flag == 9){
    //    sdhc_err("FORCE ignore IRQ here\n");
    //    sdhc_debug_flag = 0;
    //    return IRQ_HANDLED;
    //}
        
    spin_lock_irqsave(&host->mrq_lock, flags);
    victl = readl(host->base + SDHC_ICTL);
    vista = readl(host->base + SDHC_ISTA);
       
    mrq = host->mrq;
    mmc = host->mmc;
    pdata = mmc_priv(mmc);
    if(!mrq){
        sdhc_err("NULL mrq in aml_sdhc_irq step %d\n", host->xfer_step);
        if(host->xfer_step == XFER_FINISHED ||
            host->xfer_step == XFER_TIMER_TIMEOUT){
            spin_unlock_irqrestore(&host->mrq_lock, flags);
            return IRQ_HANDLED;
        }
        WARN_ON(!mrq);
        aml_sdhc_print_reg(host);
        spin_unlock_irqrestore(&host->mrq_lock, flags);
        return IRQ_HANDLED;
    }

    if ((host->xfer_step != XFER_AFTER_START) && (!host->cmd_is_stop)) {
        sdhc_err("host->xfer_step=%d\n", host->xfer_step);
        exception_flag = true;
    }

    if(host->cmd_is_stop)
        host->xfer_step = XFER_IRQ_TASKLET_BUSY;
    else
        host->xfer_step = XFER_IRQ_OCCUR;

    if (victl & vista) {
        //spin_unlock_irqrestore(&host->mrq_lock, flags);
        aml_sdhc_status(host);
        if (exception_flag)
            sdhc_err("victl=%#x, vista=%#x, status=%#x\n", victl, vista, host->status);
        //spin_lock_irqsave(&host->mrq_lock, flags);
        switch(host->status)
        {
            case HOST_RX_FIFO_FULL:
                mrq->cmd->error = -HOST_RX_FIFO_FULL;
                fifo_full_cnt++;
                break;
            case HOST_TX_FIFO_EMPTY:
                mrq->cmd->error = -HOST_TX_FIFO_EMPTY;
                fifo_empty_cnt++;
                break;
            case HOST_RSP_CRC_ERR:
            case HOST_DAT_CRC_ERR:
                mrq->cmd->error = -EILSEQ;
                break;
            case HOST_RSP_TIMEOUT_ERR:
            case HOST_DAT_TIMEOUT_ERR:
                if (!host->cmd_is_stop)
                    timeout_cnt++;
                mrq->cmd->error = -ETIMEDOUT;
                break;
            case HOST_TASKLET_DATA:
            case HOST_TASKLET_CMD:
                writel(vista, host->base+SDHC_ISTA);
                break;
            default:
                sdhc_err("Unknown irq status, victl=%#x, vista=%#x, status=%#x\n", victl, vista, host->status);
                break;
        }

        spin_unlock_irqrestore(&host->mrq_lock, flags);
        return IRQ_WAKE_THREAD;

    }
    // else{
        host->xfer_step = XFER_IRQ_UNKNOWN_IRQ;
        sdhc_err("%s Unknown Irq Ictl 0x%x, Ista 0x%x,"
            "cmd %d, xfer %d bytes \n",
            pdata->pinname, victl, vista, mrq->cmd->opcode,
            mrq->data?mrq->data->blksz*mrq->data->blocks:0);
    // }
    spin_unlock_irqrestore(&host->mrq_lock, flags);
    return IRQ_HANDLED;

// req_done:
    // cancel_delayed_work(&host->timeout);
    // spin_unlock_irqrestore(&host->mrq_lock, flags);
    // aml_sdhc_request_done(mmc, mrq);
    // return IRQ_HANDLED;
}

static void aml_sdhc_com_err_handler (struct amlsd_host* host)
{
    if(delayed_work_pending(&host->timeout))
        cancel_delayed_work(&host->timeout);
    aml_sdhc_read_response(host->mmc, host->mrq->cmd);
    aml_sdhc_print_err(host);
    aml_sdhc_host_reset(host);
    aml_sdhc_request_done(host->mmc, host->mrq);
}

static void aml_sdhc_not_timeout_err_handler (struct amlsd_host* host)
{
    //aml_sdhc_wait_ready(host, STAT_POLL_TIMEOUT);
    if(aml_sdhc_wait_ready(host, (STAT_POLL_TIMEOUT<<2))){ /*Wait command busy*/
    	sdhc_err("aml_sdhc_wait_ready error not timeout error handler\n");
    }    
    aml_sdhc_com_err_handler(host);
}

struct mmc_command aml_sdhc_cmd = {
    .opcode = MMC_STOP_TRANSMISSION,
    .flags = MMC_RSP_SPI_R1B | MMC_RSP_R1B | MMC_CMD_AC,
};
struct mmc_request aml_sdhc_stop = {
    .cmd = &aml_sdhc_cmd,
};

void aml_sdhc_send_stop(struct amlsd_host* host)
{
    struct amlsd_platform * pdata = mmc_priv(host->mmc);
    unsigned long flags;

    // sdhc_err("before cmd12\n");
   
    /*Already in mrq_lock*/
    schedule_delayed_work(&host->timeout, 50);
    spin_lock_irqsave(&host->mrq_lock, flags);
    sdhc_err_bak = host->mrq->cmd->error;
    host->mrq->cmd->error = 0;
    host->cmd_is_stop = 1;
    aml_sdhc_start_cmd(pdata, &aml_sdhc_stop);
    spin_unlock_irqrestore(&host->mrq_lock, flags);
    
}

static unsigned int clock[]={90000000,80000000,75000000,70000000,65000000,60000000,50000000};
static void aml_sdhc_set_clk_rate(struct mmc_host *mmc, unsigned int clk_ios);
irqreturn_t aml_sdhc_data_thread(int irq, void *data)
{
    struct amlsd_host* host = data;
    u32 xfer_bytes;
    struct mmc_request* mrq;
    enum aml_mmc_waitfor xfer_step;
    unsigned long flags;
    u32 vstat, status;
    struct sdhc_stat* stat = (struct sdhc_stat*)&vstat;
    struct amlsd_platform* pdata = mmc_priv(host->mmc);
    int cnt=0;
       
    u32 esta = readl(host->base + SDHC_ESTA);
    int i;        
    u32 dmc_sts = 0;
    u32 vpdma = readl(host->base+SDHC_PDMA);   
    struct sdhc_pdma* pdma = (struct sdhc_pdma*)&vpdma;


    spin_lock_irqsave(&host->mrq_lock, flags);
    mrq = host->mrq;
    xfer_step = host->xfer_step;
    status = host->status;
    
    if ((xfer_step == XFER_FINISHED) || (xfer_step == XFER_TIMER_TIMEOUT)) {
        sdhc_err("Warning: xfer_step=%d, host->status=%d\n", xfer_step, status);
        spin_unlock_irqrestore(&host->mrq_lock, flags);
        return IRQ_HANDLED;
    }

    WARN_ON((host->xfer_step != XFER_IRQ_OCCUR) && (host->xfer_step != XFER_IRQ_TASKLET_BUSY));

    if(!mrq){
        sdhc_err("!mrq xfer_step %d\n", xfer_step);
        if(xfer_step == XFER_FINISHED ||
            xfer_step == XFER_TIMER_TIMEOUT){
            spin_unlock_irqrestore(&host->mrq_lock, flags);
            return IRQ_HANDLED;
        }
        //BUG();
        aml_sdhc_print_err(host);
    }
    if(host->cmd_is_stop){
        int delay = 1;
        if (mrq->cmd->error)
            sdhc_err("cmd12 error %d\n", mrq->cmd->error);
        host->cmd_is_stop = 0;
        mrq->cmd->error = sdhc_err_bak;
        spin_unlock_irqrestore(&host->mrq_lock, flags);
        if(delayed_work_pending(&host->timeout))
            cancel_delayed_work(&host->timeout);
        msleep(delay);
        sdhc_err("delay %dms\n", delay);
        aml_sdhc_request_done(host->mmc, host->mrq);
        return IRQ_HANDLED;
    }
    spin_unlock_irqrestore(&host->mrq_lock, flags);

    BUG_ON(!host->mrq->cmd);

    switch (status) {
        case HOST_TASKLET_DATA:
            sdhc_error_flag = 0;
            BUG_ON(!mrq->data);
            if(delayed_work_pending(&host->timeout))
                cancel_delayed_work(&host->timeout);

            xfer_bytes = mrq->data->blksz*mrq->data->blocks;
            /* copy buffer from dma to data->sg in read cmd*/
            if(host->mrq->data->flags & MMC_DATA_READ){
	     	    if(!IS_MESON_M8M2_CPU){
	                if(!aml_card_type_sdio(pdata)){
	                    for(i=0; i< STAT_POLL_TIMEOUT; i++){
	                          
	                        esta = readl(host->base + SDHC_ESTA);
	                        esta = readl(host->base + SDHC_ESTA); // read twice, we just focus on the second result
	                        if(((esta >> 11) & 0x7) == 0) // REGC_ESTA[13:11]=0? then OK
	                            break;
	                        else if (i == 10)
	                            sdhc_err("SDHC_ESTA=0x%x\n", esta);
	                    }
	                   
	                    if (i == STAT_POLL_TIMEOUT) // error
	                        sdhc_err("Warning: DMA state is wrong! SDHC_ESTA=0x%x\n", esta);
	    
	                    pdma->rxfifo_manual_flush |= 0x02; // bit[30]
	                    writel(vpdma, host->base+SDHC_PDMA);
	                     //check ddr dma status after controller dma status OK
	                    for(i=0; i< STAT_POLL_TIMEOUT; i++){
#if (MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8)
				    if(IS_MESON_M8_CPU){
		                        dmc_sts = aml_read_reg32(P_MMC_CHAN_STS);
		                        dmc_sts = (dmc_sts >> 11)&1;
				    } 
				    else{
		                        dmc_sts = aml_read_reg32(P_DMC_CHAN_STS);
		                        dmc_sts = (dmc_sts >> 15)&1;  
				    }
#endif  
	                        if(dmc_sts)
	                            break;
	                        else if (i == 10)
	                            sdhc_err("SDHC_ESTA=0x%x\n", esta);
	                    }
	                   
	                    if (i == STAT_POLL_TIMEOUT) // error
	                        sdhc_err("Warning: DMA state is wrong! SDHC_ESTA=0x%x dmc_sts:%d\n", esta,dmc_sts);                                                                        
	                  
	                }                              
		   }

                aml_sg_copy_buffer(mrq->data->sg, mrq->data->sg_len, host->bn_buf,
                            xfer_bytes, 0);
                sdhc_dbg(AMLSD_DBG_RD_DATA, "R Cmd%d, arg %x, size=%d\n",
                        mrq->cmd->opcode, mrq->cmd->arg, xfer_bytes);
                sdhc_dbg(AMLSD_DBG_RD_DATA, "R Cmd %d, %x-%x-%x-%x-%x-%x-%x-%x\n",
                    host->mrq->cmd->opcode,
                    host->bn_buf[0], host->bn_buf[1],
                    host->bn_buf[2], host->bn_buf[3],
                    host->bn_buf[4], host->bn_buf[5],
                    host->bn_buf[6], host->bn_buf[7]);
                // aml_debug_print_buf(host->bn_buf, xfer_bytes);
            }

            vstat = readl(host->base + SDHC_STAT);
            if (stat->rxfifo_cnt) {
                sdhc_err("cmd%d, rxfifo_cnt=%d\n", mrq->cmd->opcode, stat->rxfifo_cnt);
            }
            // if (stat->txfifo_cnt) {
                // sdhc_err("cmd%d, txfifo_cnt=%d\n", mrq->cmd->opcode, stat->txfifo_cnt);
            // }

            spin_lock_irqsave(&host->mrq_lock, flags);
            mrq->cmd->error = 0;
            mrq->data->bytes_xfered = xfer_bytes;
            host->xfer_step = XFER_TASKLET_DATA;
            spin_unlock_irqrestore(&host->mrq_lock, flags);
            //do not check device ready status here
            //aml_sdhc_wait_ready(host, STAT_POLL_TIMEOUT);
            if(aml_sdhc_wait_ready(host, (STAT_POLL_TIMEOUT<<2))){ /*Wait command busy*/
            	sdhc_err("aml_sdhc_wait_ready error after data thread\n");
            }            
            aml_sdhc_read_response(host->mmc, mrq->cmd);
            // aml_sdhc_clear_fifo(host);
            aml_sdhc_request_done(host->mmc, mrq);
            break;
        case HOST_TASKLET_CMD:
            sdhc_error_flag = 0;
            if(!host->mrq->data){
                if(delayed_work_pending(&host->timeout))
                    cancel_delayed_work(&host->timeout);
                spin_lock_irqsave(&host->mrq_lock, flags);
                host->mrq->cmd->error = 0;
                host->xfer_step = XFER_TASKLET_CMD;
                spin_unlock_irqrestore(&host->mrq_lock, flags);
                //aml_sdhc_wait_ready(host, STAT_POLL_TIMEOUT);
	            if(aml_sdhc_wait_ready(host, STAT_POLL_TIMEOUT)){ /*Wait command busy*/
	            	sdhc_err("aml_sdhc_wait_ready error cmd thread\n");
	            }                
                aml_sdhc_read_response(host->mmc, host->mrq->cmd);
                aml_sdhc_request_done(host->mmc, mrq);
            } else {
                sdhc_err("xfer_step is HOST_TASKLET_CMD, while host->mrq->data is not NULL\n");
            }
            break;
        case HOST_TX_FIFO_EMPTY:
        case HOST_RX_FIFO_FULL:   
        case HOST_RSP_TIMEOUT_ERR:
        case HOST_DAT_TIMEOUT_ERR:                                 
            if(delayed_work_pending(&host->timeout))                            
                cancel_delayed_work(&host->timeout);
            //aml_sdhc_wait_ready(host, STAT_POLL_TIMEOUT);
            if(aml_sdhc_wait_ready(host, (STAT_POLL_TIMEOUT<<2))){ /*Wait command busy*/
            	sdhc_err("aml_sdhc_wait_ready error fifo or timeout thread\n");
            }             
            aml_sdhc_read_response(host->mmc, host->mrq->cmd);
            aml_sdhc_print_err(host);
            aml_sdhc_host_reset(host);
            writel(SDHC_ISTA_W1C_ALL, host->base+SDHC_ISTA);
            spin_lock_irqsave(&host->mrq_lock, flags);
            if((sdhc_error_flag == 0) && (host->mrq->cmd->opcode != MMC_SEND_TUNING_BLOCK) 
                    && (host->mrq->cmd->opcode != MMC_SEND_TUNING_BLOCK_HS200) 
                    && host->mrq->data){  //set cmd retry cnt when first error.
                sdhc_error_flag |= (1<<0);
                if((status == HOST_RSP_TIMEOUT_ERR) || (status == HOST_DAT_TIMEOUT_ERR)){
                    if(aml_card_type_mmc(pdata)){
                        mrq->cmd->retries = AML_TIMEOUT_RETRY_COUNTER; 
                    }
                    else{
                        sdhc_error_flag = 0;
                        mrq->cmd->retries = 0;
                    }
                }
                else{
                    mrq->cmd->retries = AML_ERROR_RETRY_COUNTER;                     
                }
            } 
            
            if(sdhc_error_flag && (mrq->cmd->retries == 0)){
                sdhc_error_flag |= (1<<30);
                sdhc_err("Command retried failed line:%d, status:%d\n", __LINE__, status);
            }    
            spin_unlock_irqrestore(&host->mrq_lock, flags);
                          
            //do not send stop for sdio wifi case 
            if(host->mrq->stop && aml_card_type_mmc(pdata) && pdata->is_in
                && (host->mrq->cmd->opcode != MMC_SEND_TUNING_BLOCK) 
                    && (host->mrq->cmd->opcode != MMC_SEND_TUNING_BLOCK_HS200)){
            //if((host->mrq->cmd->opcode != SD_IO_RW_DIRECT) && (host->mrq->cmd->opcode != SD_IO_RW_EXTENDED) 
           //         && (!mmc_card_removed(pdata->mmc->card)) && (!mrq->data)){
                aml_sdhc_send_stop(host);                
            }
            else{
                aml_sdhc_request_done(host->mmc, mrq);
            }
            break;
        //case HOST_RX_FIFO_FULL:
        //    aml_sdhc_not_timeout_err_handler(host);
        //    break;
        case HOST_RSP_CRC_ERR:
        case HOST_DAT_CRC_ERR:
            pdata = mmc_priv(host->mmc);
            if (aml_card_type_sdio(pdata)  // sdio_wifi
                    && (host->mrq->cmd->opcode != MMC_SEND_TUNING_BLOCK) 
                    && (host->mrq->cmd->opcode != MMC_SEND_TUNING_BLOCK_HS200)) {
                sdhc_err("host->mmc->ios.clock:%d\n",host->mmc->ios.clock);
                while(host->mmc->ios.clock<=clock[cnt])
                {
                    cnt++;
                    if(cnt >= (ARRAY_SIZE(clock) - 1))
                        break;
                }
                spin_lock_irqsave(&host->mrq_lock, flags);

                host->mmc->ios.clock = clock[cnt]; 
                pdata->need_retuning = true; // retuing will be done in the next request
                mrq->cmd->retries = (ARRAY_SIZE(clock) - 1) - cnt;
                spin_unlock_irqrestore(&host->mrq_lock, flags);
                aml_sdhc_set_clk_rate(host->mmc,host->mmc->ios.clock);


            }
            else if(aml_card_type_mmc(pdata) && (host->mrq->cmd->opcode != MMC_SEND_TUNING_BLOCK) && (host->mrq->cmd->opcode != MMC_SEND_TUNING_BLOCK_HS200)){
                spin_lock_irqsave(&host->mrq_lock, flags);

                if(sdhc_error_flag == 0){  //set cmd retry cnt when first error.
                    sdhc_error_flag |= (1<<1);
                    mrq->cmd->retries = AML_ERROR_RETRY_COUNTER; 
                }                 
                spin_unlock_irqrestore(&host->mrq_lock, flags);                                          
            } 
            if(sdhc_error_flag && (mrq->cmd->retries == 0)){
                sdhc_error_flag |= (1<<30);
                //sdhc_err("Command retried failed\n");
            }       
    
            aml_sdhc_not_timeout_err_handler(host);
            break;

        default:
            sdhc_err("BUG xfer_step=%d, host->status=%d\n", xfer_step, status);
            aml_sdhc_print_err(host);
            //BUG();
    }
    
    return IRQ_HANDLED;
}

static void aml_sdhc_clk_switch_off (struct amlsd_host* host)
{
    u32 vclkc = readl(host->base+SDHC_CLKC);
    struct sdhc_clkc* clkc = (struct sdhc_clkc*)&vclkc;

    if (host->is_gated) {
        // sdhc_err("direct return\n");
        return;
    }

    /*Turn off Clock*/
    clkc->tx_clk_on = 0;
    clkc->rx_clk_on = 0;
    clkc->sd_clk_on = 0;
    writel(vclkc, host->base+SDHC_CLKC);
    clkc->mod_clk_on = 0;
    writel(vclkc, host->base+SDHC_CLKC);

    host->is_gated = true;
    // sdhc_err("clock off\n");
}

static void aml_sdhc_clk_switch_on (struct amlsd_platform* pdata, int clk_div, int clk_src_sel)
{
    struct amlsd_host* host = (void*)pdata->host;
    u32 vclkc = readl(host->base+SDHC_CLKC);
    struct sdhc_clkc* clkc = (struct sdhc_clkc*)&vclkc;

    /*Set clock divide*/
    clkc->clk_div = clk_div;
    clkc->clk_src_sel = clk_src_sel;
    writel(vclkc, host->base+SDHC_CLKC);

    /*Turn on Clock*/
    clkc->mod_clk_on = 1;
    writel(vclkc, host->base+SDHC_CLKC);

    clkc->tx_clk_on = 1;
    clkc->rx_clk_on = 1;
    clkc->sd_clk_on = 1;
    writel(vclkc, host->base+SDHC_CLKC);

    host->is_gated = false;
    // udelay(10);
    // sdhc_err("clock on, SDHC_CLKC=%#x\n", readl(host->base+SDHC_CLKC));
}

static void aml_sdhc_clk_switch (struct amlsd_platform* pdata, int clk_div, int clk_src_sel)
{
    struct amlsd_host* host = (void*)pdata->host;
    u32 vclkc = readl(host->base + SDHC_CLKC);
    struct sdhc_clkc* clkc = (struct sdhc_clkc*)&vclkc;
    
    if (!host->is_gated && (clkc->clk_div == clk_div) && (clkc->clk_src_sel == clk_src_sel)) {
        // sdhc_err("direct return\n");
        return; // if the same, return directly
    }

    aml_sdhc_clk_switch_off(host);
    // mdelay(1);
    aml_sdhc_clk_switch_on(pdata, clk_div, clk_src_sel);
}

/*
1. clock valid range
2. clk config enable
3. select clock source
4. set clock divide
*/
static void aml_sdhc_set_clk_rate(struct mmc_host *mmc, unsigned int clk_ios)
{
    u32 clk_rate, clk_div, clk_src_sel, clk_src_div;
    unsigned long flags;
    struct clk *clk_src;
    struct amlsd_platform* pdata = mmc_priv(mmc);
    struct amlsd_host* host = (void*)pdata->host;
    u32 vclk2;
    struct sdhc_clk2 *clk2 = (struct sdhc_clk2*)&vclk2;

    if (clk_ios == 0) {
        aml_sdhc_clk_switch_off(host);
        return;
    }

    if ((clk_ios > 100000000) && (val1 > 100000000)) // for debug, 100M
        clk_ios = val1;

    clk_src_div = -1;
    clk_src_sel = SDHC_CLOCK_SRC_FCLK_DIV3;
    switch(clk_src_sel)
    {
        case SDHC_CLOCK_SRC_FCLK_DIV3:
            clk_src_div = 3;
            break;
        case SDHC_CLOCK_SRC_FCLK_DIV4:
            clk_src_div = 4;
            break;
        case SDHC_CLOCK_SRC_FCLK_DIV5:
            clk_src_div = 5;
            break;
        case SDHC_CLOCK_SRC_OSC:
            clk_src_div = 0;
            break;
        default:
            sdhc_err("Clock source error: %d\n", clk_src_sel);
            return; // clk_src_div = -1;
    }

    if (clk_src_sel != SDHC_CLOCK_SRC_OSC) {
        clk_src = clk_get_sys("pll_fixed", NULL); // fclk
        clk_rate = clk_get_rate(clk_src)/clk_src_div;
    } else { // OSC, 24MHz
        clk_rate = 24000000;
    }
	
		spin_lock_irqsave(&host->mrq_lock, flags);
		
    if(clk_ios > pdata->f_max)
        clk_ios = pdata->f_max;
    if(clk_ios < pdata->f_min)
        clk_ios = pdata->f_min;

    /*0: dont set it, 1:div2, 2:div3, 3:div4...*/
    clk_div = clk_rate / clk_ios - !(clk_rate%clk_ios);
#if !defined(CONFIG_MACH_MESON8B_ODROIDC)
    if (!(clk_div & 0x01)) // if even number, turn it to an odd one
        clk_div++;
#endif

    aml_sdhc_clk_switch(pdata, clk_div, clk_src_sel);
    pdata->clkc = readl(host->base+SDHC_CLKC);

    pdata->mmc->actual_clock = clk_rate / (clk_div + 1);

    vclk2 = readl(host->base+SDHC_CLK2);
    clk2->sd_clk_phase = 1; // 1
    if (pdata->mmc->actual_clock > 100000000) { // if > 100M
        clk2->rx_clk_phase = rx_clk_phase_set;
        // clk2->sd_clk_phase = sd_clk_phase_set; // 1
    } else if (pdata->mmc->actual_clock > 45000000) { // if > 45M
        if (mmc->ios.signal_voltage == MMC_SIGNAL_VOLTAGE_330) // 3.3V
            clk2->rx_clk_phase = 15;
        else
            clk2->rx_clk_phase = 11;
    } else if (pdata->mmc->actual_clock >= 25000000) { // if >= 25M
        clk2->rx_clk_phase = 15; // 10
    } else if (pdata->mmc->actual_clock > 5000000) { // if > 5M
        clk2->rx_clk_phase = 23;
    } else if (pdata->mmc->actual_clock > 1000000) { // if > 1M
        clk2->rx_clk_phase = 55;
    } else {
        clk2->rx_clk_phase = 1061; // 63; // 24;
    }
    writel(vclk2, host->base+SDHC_CLK2);
    pdata->clk2 = vclk2;

    /*Disable All Irq*/
    writel(0, host->base+SDHC_ICTL);

    /*Wait for a while after clock setting*/
    // udelay(100);
		
		spin_unlock_irqrestore(&host->mrq_lock, flags);
    sdhc_dbg(AMLSD_DBG_IOS, "Clk IOS %d, Clk Src %d, Host Max Clk %d, vclkc=%#x, clk2=%#x, actual_clock=%d, rx_clk_phase=%d, sd_clk_phase=%d\n",
        clk_ios, clk_rate, pdata->f_max, readl(host->base+SDHC_CLKC), readl(host->base+SDHC_CLK2), pdata->mmc->actual_clock, clk2->rx_clk_phase, clk2->sd_clk_phase);

    return;
}

/*setup bus width, 1bit, 4bits, 8bits*/
static void aml_sdhc_set_bus_width(struct amlsd_platform* pdata, u32 busw_ios)
{
    struct amlsd_host* host = (void*)pdata->host;
    u32 vctrl = readl(host->base + SDHC_CTRL);
    struct sdhc_ctrl* ctrl = (struct sdhc_ctrl*)&vctrl;
    u32 width = 0;

    switch(busw_ios)
    {
        case MMC_BUS_WIDTH_1:
            width = 0;
            break;
        case MMC_BUS_WIDTH_4:
            width = 1;
            break;
        case MMC_BUS_WIDTH_8:
            width = 2;
            break;
        default:
            sdhc_err("Error Data Bus\n");
            break;
    }

    ctrl->dat_type = width;
    pdata->width = width;
    writel(vctrl, host->base+SDHC_CTRL);
    sdhc_dbg(AMLSD_DBG_IOS, "Bus Width Ios %d\n", busw_ios);
}

/*call by mmc, power on, power off ...*/
static void aml_sdhc_set_power(struct amlsd_platform* pdata, u32 power_mode)
{
    switch (power_mode) {
        case MMC_POWER_ON:
            if(pdata->pwr_pre)
                pdata->pwr_pre(pdata);
            if(pdata->pwr_on)
                    pdata->pwr_on(pdata);
            break;
        case MMC_POWER_UP:
            break;
        case MMC_POWER_OFF:
        default:
            if(pdata->pwr_pre)
                pdata->pwr_pre(pdata);
            if(pdata->pwr_off)
                pdata->pwr_off(pdata);
            break;
    }
}

/*call by mmc, set ios: power, clk, bus width*/
static void aml_sdhc_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
    struct amlsd_platform * pdata = mmc_priv(mmc);

    if(!pdata->is_in)
        return;

    /*Set Power*/
    aml_sdhc_set_power(pdata, ios->power_mode);

    /*Set Clock*/
    aml_sdhc_set_clk_rate(mmc, ios->clock);

    /*Set Bus Width*/
    aml_sdhc_set_bus_width(pdata, ios->bus_width);

    if (ios->chip_select == MMC_CS_HIGH) {
        aml_cs_high(pdata);
    } else if (ios->chip_select == MMC_CS_DONTCARE) {
        aml_cs_dont_care(pdata);
    } else { // MMC_CS_LOW
        /* Nothing to do */
    }
}

/*get readonly: 0 for rw, 1 for ro*/
static int aml_sdhc_get_ro(struct mmc_host *mmc)
{
    struct amlsd_platform * pdata = mmc_priv(mmc);
    u32 ro = 0;

    if(pdata->ro)
        ro = pdata->ro(pdata);
    return ro;
}

/*get card detect: 1 for insert, 0 for removed*/
int aml_sdhc_get_cd(struct mmc_host *mmc)
{
    struct amlsd_platform * pdata = mmc_priv(mmc);
    return pdata->is_in; // 0: no inserted  1: inserted
}

int aml_signal_voltage_switch (struct mmc_host *mmc, struct mmc_ios *ios)
{
    struct amlsd_platform* pdata = mmc_priv(mmc);

    return aml_sd_voltage_switch(pdata, ios->signal_voltage);
}

/* Check if the card is pulling dat[0:3] low */
static int aml_sdhc_card_busy (struct mmc_host *mmc)
{
    struct amlsd_platform * pdata = mmc_priv(mmc);
    struct amlsd_host *host = pdata->host;
    u32 vstat;
    struct sdhc_stat* stat = (struct sdhc_stat*)&vstat;

    vstat = readl(host->base + SDHC_STAT);
    sdhc_dbg(AMLSD_DBG_COMMON, "dat[0:3]=%#x\n", stat->dat3_0);

    return (stat->dat3_0 == 0);
}

#ifdef CONFIG_PM

static int aml_sdhc_suspend(struct platform_device *pdev, pm_message_t state)
{
    int ret = 0;
    int i;
    struct amlsd_host *host = platform_get_drvdata(pdev);
    struct mmc_host* mmc;
    struct amlsd_platform* pdata;

    printk("***Entered %s:%s\n", __FILE__,__func__);
    i = 0;
    list_for_each_entry(pdata, &host->sibling, sibling){
        cancel_delayed_work(&pdata->retuning);
        pdata->need_retuning = false;
        
        mmc = pdata->mmc;
        //mmc_power_save_host(mmc);
        ret = mmc_suspend_host(mmc);
        if (ret)
            break;

        i++;
    }

    if (ret) {
        list_for_each_entry(pdata, &host->sibling, sibling) {
            i--;
            if (i < 0) {
                break;
            }
            mmc = pdata->mmc;
            mmc_resume_host(mmc);
        }
    }
    printk("***Exited %s:%s\n", __FILE__,__func__);

    return ret;
}

static int aml_sdhc_resume(struct platform_device *pdev)
{
    int ret = 0;
    struct amlsd_host *host = platform_get_drvdata(pdev);
    struct mmc_host* mmc;
    struct amlsd_platform* pdata;

    printk("***Entered %s:%s\n", __FILE__,__func__);
    list_for_each_entry(pdata, &host->sibling, sibling){
        // detect if a card is exist or not if it is removable
        if (!(pdata->caps & MMC_CAP_NONREMOVABLE)) {
            aml_sd_uart_detect(pdata);
        }
        mmc = pdata->mmc;
        //mmc_power_restore_host(mmc);
        ret = mmc_resume_host(mmc);
        if (ret)
            break;
    }
    printk("***Exited %s:%s\n", __FILE__,__func__);
    return ret;
}

#else

#define aml_sdhc_suspend    NULL
#define aml_sdhc_resume        NULL

#endif

static const struct mmc_host_ops aml_sdhc_ops = {
    .request = aml_sdhc_request,
    .set_ios = aml_sdhc_set_ios,
    .get_cd = aml_sdhc_get_cd,
    .get_ro = aml_sdhc_get_ro,
    .start_signal_voltage_switch = aml_signal_voltage_switch,
    .card_busy = aml_sdhc_card_busy,
    .execute_tuning = aml_sdhc_execute_tuning,
    .hw_reset = aml_emmc_hw_reset,
};

/*for multi host claim host*/
static struct mmc_claim aml_sdhc_claim;

static ssize_t sdhc_debug_func(struct class *class, struct class_attribute *attr, const char *buf, size_t count)
{
//    struct amlsd_host *host = container_of(class, struct amlsd_host, debug);
    
    sscanf(buf, "%x", &sdhc_debug_flag);    
    printk("sdhc_debug_flag: %d\n", sdhc_debug_flag);

    return count;
}
            
static ssize_t show_sdhc_debug(struct class *class,
                    struct class_attribute *attr,	char *buf)
{
//    struct amlsd_host *host = container_of(class, struct amlsd_host, debug);
    
    printk("sdhc_debug_flag: %d\n", sdhc_debug_flag);
    
    printk("1 : Force sdhc HOST_TX_FIFO_EMPTY error \n");
    printk("2 : Force sdhc HOST_RX_FIFO_FULL error \n");
    printk("3 : Force sdhc HOST_RSP_CRC_ERR error \n");
    printk("4 : Force sdhc HOST_DAT_CRC_ERR error \n");
    printk("5 : Force sdhc HOST_DAT_TIMEOUT_ERR error \n");
    printk("6 : Force sdhc HOST_RSP_TIMEOUT_ERR error \n");

    printk("9 : Force sdhc irq timeout error \n");
    
    return 0;
}

static struct class_attribute sdhc_class_attrs[] = {
    __ATTR(debug,  S_IRUGO | S_IWUSR , show_sdhc_debug, sdhc_debug_func),
    __ATTR_NULL
};

static struct amlsd_host* aml_sdhc_init_host(void)
{
    struct amlsd_host *host;

    spin_lock_init(&aml_sdhc_claim.lock);
    init_waitqueue_head(&aml_sdhc_claim.wq);

    host = kzalloc(sizeof(struct amlsd_host), GFP_KERNEL);

    if(request_threaded_irq(INT_SDHC, aml_sdhc_irq,
            aml_sdhc_data_thread, IRQF_DISABLED, "sdhc", (void*)host)){
        sdhc_err("Request SDHC Irq Error!\n");
        return NULL;
    }

    host->bn_buf = dma_alloc_coherent(NULL, SDHC_BOUNCE_REQ_SIZE,
                            &host->bn_dma_buf, GFP_KERNEL);
    // sdhc_err("host->bn_buf %x, host->bn_dma_buf %x\n", (int)host->bn_buf,
                // (int)host->bn_dma_buf);
    if(NULL == host->bn_buf){
        sdhc_err("Dma alloc Fail!\n");
        return NULL;
    }
    // setup_timer(&host->timeout_tlist, aml_sdhc_timeout, (ulong)host);
    INIT_DELAYED_WORK(&host->timeout, aml_sdhc_timeout);

    spin_lock_init(&host->mrq_lock);
    host->xfer_step = XFER_INIT;

    INIT_LIST_HEAD(&host->sibling);
    
    host->init_flag = 1;
    
    host->version = AML_MMC_VERSION;
    host->storage_flag = storage_flag;
    host->pinctrl = NULL;
    host->is_gated = false;
    host->status = HOST_INVALID;
    host->msg_buf = kmalloc(MESSAGE_BUF_SIZE, GFP_KERNEL);
    if (!host->msg_buf) {
        printk("malloc message buffer fail\n");
    }

#ifdef      CONFIG_MMC_AML_DEBUG
    host->req_cnt = 0;
    sdhc_err("CONFIG_MMC_AML_DEBUG is on!\n");
#endif

#ifdef      CONFIG_AML_MMC_DEBUG_FORCE_SINGLE_BLOCK_RW
    sdhc_err("CONFIG_AML_MMC_DEBUG_FORCE_SINGLE_BLOCK_RW is on!\n");
#endif

	host->debug.name = kzalloc(strlen((const char*)AML_SDHC_MAGIC)+1, GFP_KERNEL);
	strcpy((char *)(host->debug.name), (const char*)AML_SDHC_MAGIC);
	host->debug.class_attrs = sdhc_class_attrs;
	if(class_register(&host->debug))
		printk(" class register nand_class fail!\n");
		
    return host;
}

static int aml_sdhc_probe(struct platform_device *pdev)
{
    struct mmc_host *mmc = NULL;
    struct amlsd_host *host = NULL;
    struct amlsd_platform* pdata;
    int ret = 0, i;

    // pre_probe_host_ops(); // for tmp debug

    aml_mmc_ver_msg_show();

    host = aml_sdhc_init_host();
    if(!host)
        goto fail_init_host;
    if(amlsd_get_reg_base(pdev, host))
        goto fail_init_host;

    aml_sdhc_reg_init(host);
    host->pdev = pdev;

    for(i=0;i<MMC_MAX_DEVICE;i++){
        /*malloc extra amlsd_platform*/
        mmc = mmc_alloc_host(sizeof(struct amlsd_platform), &pdev->dev);
        if (!mmc) {
            ret = -ENOMEM;
            goto probe_free_host;
        }

        pdata = mmc_priv(mmc);
        memset(pdata, 0, sizeof(struct amlsd_platform));
        if(amlsd_get_platform_data(pdev, pdata, mmc, i)) {
            mmc_free_host(mmc);
            break;
        }
        if (pdata->port == PORT_SDHC_C) {
            if (is_emmc_exist(host)) {
                mmc->is_emmc_port = 1;
            } else { // there is not eMMC/tsd
                printk("[%s]: there is not eMMC/tsd, skip sdhc_c dts config!\n", __FUNCTION__);
                i++; // skip the port written in the dts
                memset(pdata, 0, sizeof(struct amlsd_platform));
                if(amlsd_get_platform_data(pdev, pdata, mmc, i)) {
                    mmc_free_host(mmc);
                    break;
                }
            }
        }
        dev_set_name(&mmc->class_dev, "%s", pdata->pinname);
        INIT_DELAYED_WORK(&pdata->retuning, aml_sdhc_tuning_timer);
        if (pdata->caps & MMC_CAP_NONREMOVABLE) {
            pdata->is_in = true;
        }
        if (pdata->caps & MMC_PM_KEEP_POWER)
            mmc->pm_caps |= MMC_PM_KEEP_POWER; 
        pdata->host = host;
        pdata->mmc = mmc;
        pdata->is_fir_init = true;
        pdata->is_tuned = false;
        pdata->need_retuning = false;
        pdata->signal_voltage = 0xff; // init as an invalid value

        mmc->index = i;
        mmc->ops = &aml_sdhc_ops;
        mmc->alldev_claim = &aml_sdhc_claim;
        mmc->ios.clock = 400000;
        mmc->ios.bus_width = MMC_BUS_WIDTH_1;
        mmc->max_blk_count = 4095; //
        mmc->max_blk_size = 4095; //
        mmc->max_req_size = pdata->max_req_size;
        mmc->max_seg_size = mmc->max_req_size;
        mmc->max_segs = 1024;
        mmc->ocr_avail = pdata->ocr_avail;
        mmc->ocr = pdata->ocr_avail;
        mmc->caps = pdata->caps;
        mmc->caps2 = pdata->caps2;
        mmc->f_min = pdata->f_min;
        mmc->f_max = pdata->f_max;
        mmc->max_current_180 = 300; // 300 mA in 1.8V
        mmc->max_current_330 = 300; // 300 mA in 3.3V

        if (aml_card_type_sdio(pdata)) { // if sdio_wifi
            mmc->host_rescan_disable = true;
			mmc->rescan_entered = 1; // do NOT run mmc_rescan for the first time
        } else {
            mmc->host_rescan_disable = false;
			mmc->rescan_entered = 0; 
        }

        if(pdata->port_init)
            pdata->port_init(pdata);

        aml_sduart_pre(pdata);

        ret = mmc_add_host(mmc);
        if (ret) { // error
            sdhc_err("Failed to add mmc host.\n");
            goto probe_free_host;
        } else { // ok
            if (aml_card_type_sdio(pdata)) { // if sdio_wifi
                sdio_host = mmc;
                //mmc->rescan_entered = 1; // do NOT run mmc_rescan for the first time
            }
        }

        aml_sdhc_init_debugfs(mmc);
        /*Add each mmc host pdata to this controller host list*/
        INIT_LIST_HEAD(&pdata->sibling);
        list_add_tail(&pdata->sibling, &host->sibling);

        /*Register card detect irq : plug in & unplug*/
        if(pdata->irq_in && pdata->irq_out){
            pdata->irq_init(pdata);
            ret = request_threaded_irq(pdata->irq_in+INT_GPIO_0,
                    (irq_handler_t)aml_sd_irq_cd, aml_irq_cd_thread,
                    IRQF_DISABLED, "sdhc_mmc_in", (void*)pdata);
            if (ret) {
                sdhc_err("Failed to request mmc IN detect\n");
                goto probe_free_host;
            }
            ret |= request_threaded_irq(pdata->irq_out+INT_GPIO_0,
                    (irq_handler_t)aml_sd_irq_cd, aml_irq_cd_thread,
                    IRQF_DISABLED, "sdhc_mmc_out", (void*)pdata);
            // ret = request_irq(pdata->irq_in+INT_GPIO_0, aml_sdhc_irq_cd,
                        // IRQF_DISABLED, "sdhc_mmc_in", pdata);
            // ret |= request_irq(pdata->irq_out+INT_GPIO_0, aml_sdhc_irq_cd,
                        // IRQF_DISABLED, "sdhc_mmc_out", pdata);
            if (ret) {
                sdhc_err("Failed to request mmc OUT detect\n");
                goto fail_cd_irq_in;
            }
        }
    }

    print_tmp("%s() success!\n", __FUNCTION__);
    platform_set_drvdata(pdev, host);
    return 0;

fail_cd_irq_in:
    if(pdata->irq_in)
        free_irq(pdata->irq_in, pdata);
probe_free_host:
    list_for_each_entry(pdata, &host->sibling, sibling){
        mmc = pdata->mmc;
        mmc_remove_host(mmc);
        mmc_free_host(mmc);
    }
fail_init_host:
    iounmap(host->base);
    free_irq(INT_SDHC, host);
    dma_free_coherent(NULL, SDHC_BOUNCE_REQ_SIZE, host->bn_buf,
        (dma_addr_t)host->bn_dma_buf);
    kfree(host);
    print_tmp("aml_sdhc_probe() fail!\n");
    return ret;
}

int aml_sdhc_remove(struct platform_device *pdev)
{
    struct amlsd_host* host  = platform_get_drvdata(pdev);
    struct mmc_host* mmc;
    struct amlsd_platform* pdata;

    dma_free_coherent(NULL, SDHC_BOUNCE_REQ_SIZE, host->bn_buf,
        (dma_addr_t )host->bn_dma_buf);

    free_irq(INT_SDHC, host);
    iounmap(host->base);

    list_for_each_entry(pdata, &host->sibling, sibling){
        mmc = pdata->mmc;
        mmc_remove_host(mmc);
        mmc_free_host(mmc);
    }
    
    aml_devm_pinctrl_put(host);

    kfree(host->msg_buf);
    kfree(host);

    switch_mod_gate_by_type(MOD_SDHC, 0); // gate clock of SDHC

    return 0;
}

static const struct of_device_id aml_sdhc_dt_match[]={
    {
        .compatible = "amlogic,aml_sdhc",
    },
    {},
};

MODULE_DEVICE_TABLE(of, aml_sdhc_dt_match);

static struct platform_driver aml_sdhc_driver = {
    .probe         = aml_sdhc_probe,
    .remove        = aml_sdhc_remove,
    .suspend    = aml_sdhc_suspend,
    .resume        = aml_sdhc_resume,
    .driver        = {
        .name = "aml_sdhc",
        .owner = THIS_MODULE,
        .of_match_table=aml_sdhc_dt_match,
    },
};

static int __init aml_sdhc_init(void)
{
    return platform_driver_register(&aml_sdhc_driver);
}

static void __exit aml_sdhc_cleanup(void)
{
    platform_driver_unregister(&aml_sdhc_driver);
}

module_init(aml_sdhc_init);
module_exit(aml_sdhc_cleanup);

MODULE_DESCRIPTION("Amlogic Multimedia Card driver");
MODULE_LICENSE("GPL");

static int __init rx_clk_phase_setup(char *str)
{
    rx_clk_phase_set = simple_strtol(str, NULL, 0);
    print_dbg("rx_clk_phase=%d\n", rx_clk_phase_set);
    return 1;
}
__setup("rx_clk_phase=", rx_clk_phase_setup);

static int __init sd_clk_phase_setup(char *str)
{
    sd_clk_phase_set = simple_strtol(str, NULL, 0);
    print_dbg("sd_clk_phase_set=%d\n", sd_clk_phase_set);
    return 1;
}
__setup("sd_clk_phase=", sd_clk_phase_setup);

static int __init rx_endian_setup(char *str)
{
    rx_endian = simple_strtol(str, NULL, 0);
    print_dbg("rx_endian=%#x\n", rx_endian);
    return 1;
}
__setup("rx_endian=", rx_endian_setup);

static int __init tx_endian_setup(char *str)
{
    tx_endian = simple_strtol(str, NULL, 0);
    print_dbg("tx_endian=%#x\n", tx_endian);
    
    return 1;
}
__setup("tx_endian=", tx_endian_setup);

static int __init val1_setup(char *str)
{
    val1 = simple_strtol(str, NULL, 0);
    print_dbg("val1=%d\n", val1);
    
    return 1;
}
__setup("val1=", val1_setup);
