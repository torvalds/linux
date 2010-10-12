/* arch/arm/mach-rk2818/dma.c
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/dma-mapping.h>

#include <asm/system.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <mach/dma.h>
#include <mach/rk2818_iomap.h>


static struct rk2818_dma rk2818_dma[MAX_DMA_CHANNELS];

static struct tasklet_struct rk2818_dma_tasklet;

const static char *rk28_dma_dev_id[] = {
    "sd_mmc",
    "uart_2",
    "uart_3",
    "sdio",
    "i2s",
    "spi_m",
    "spi_s",
    "uart_0",
    "uart_1",
#ifdef test_dma    
    "mobile_sdram"
#endif    
};  

const static struct rk28_dma_dev rk28_dev_info[] = {
	[RK28_DMA_SD_MMC] = {
		.hd_if_r     = RK28_DMA_SD_MMC0,
        .hd_if_w     = RK28_DMA_SD_MMC0,
        .dev_addr_r  = RK2818_SDMMC0_PHYS + 0x100,
		.dev_addr_w	 = RK2818_SDMMC0_PHYS + 0x100,
		.fifo_width  = 32,
	},
	[RK28_DMA_URAT2] = {
	    .hd_if_r     = RK28_DMA_URAT2_RXD,
        .hd_if_w     = RK28_DMA_URAT2_TXD,
		.dev_addr_r	 = RK2818_UART2_PHYS,
        .dev_addr_w  = RK2818_UART2_PHYS,
		.fifo_width  = 32,
	},
	[RK28_DMA_URAT3] = {
        .hd_if_r     = RK28_DMA_URAT3_RXD,
        .hd_if_w     = RK28_DMA_URAT3_TXD,
        .dev_addr_r  = RK2818_UART3_PHYS,
        .dev_addr_w  = RK2818_UART3_PHYS,
		.fifo_width  = 32,
	},
	[RK28_DMA_SDIO] = {

	    .hd_if_r     = RK28_DMA_SD_MMC1,
        .hd_if_w     = RK28_DMA_SD_MMC1,
		.dev_addr_r	 = RK2818_SDMMC1_PHYS + 0x100,
        .dev_addr_w  = RK2818_SDMMC1_PHYS + 0x100,
		.fifo_width  = 32,
	},
	[RK28_DMA_I2S] = {
        .hd_if_r     = RK28_DMA_I2S_RXD,
        .hd_if_w     = RK28_DMA_I2S_TXD,
        .dev_addr_r  = RK2818_I2S_PHYS + 0x04,
        .dev_addr_w  = RK2818_I2S_PHYS + 0x08,
		.fifo_width  = 32,
	},
	[RK28_DMA_SPI_M] = {
        .hd_if_r     = RK28_DMA_SPI_M_RXD,
        .hd_if_w     = RK28_DMA_SPI_M_TXD,
        .dev_addr_r  = RK2818_SPIMASTER_PHYS + 0x60,
        .dev_addr_w  = RK2818_SPIMASTER_PHYS + 0x60,
        .fifo_width  = 8,
	},
	[RK28_DMA_SPI_S] = {
        .hd_if_r     = RK28_DMA_SPI_S_RXD,
        .hd_if_w     = RK28_DMA_SPI_S_TXD,
        .dev_addr_r  = RK2818_SPISLAVE_PHYS + 0x60,
        .dev_addr_w  = RK2818_SPISLAVE_PHYS + 0x60,
        .fifo_width  = 8,
	},
	[RK28_DMA_URAT0] = {
        .hd_if_r     = RK28_DMA_URAT0_RXD,
        .hd_if_w     = RK28_DMA_URAT0_TXD,
        .dev_addr_r  = RK2818_UART0_PHYS,
        .dev_addr_w  = RK2818_UART0_PHYS,
		.fifo_width  = 8,
	},
	[RK28_DMA_URAT1] = {
        .hd_if_r     = RK28_DMA_URAT1_RXD,
        .hd_if_w     = RK28_DMA_URAT1_TXD,
        .dev_addr_r  = RK2818_UART1_PHYS,
        .dev_addr_w  = RK2818_UART1_PHYS,
		.fifo_width  = 8,
	},
#ifdef test_dma	
    [RK28_DMA_SDRAM] = {
        .hd_if_r     = 0,
        .hd_if_w     = 0,
        .dev_addr_r  = BUF_READ_ARRR,
        .dev_addr_w  = BUF_WRITE_ARRR,
        .fifo_width  = 32,
    },
#endif    

};




/**
 * rk28_dma_ctl_for_write - set dma control register for writing mode   
 *
 */
static inline unsigned int rk28_dma_ctl_for_write(unsigned int dma_ch, const struct rk28_dma_dev *dev_info, dma_t *dma_t)
{    
#ifdef test_dma
    unsigned int dev_mode = B_CTLL_MEM2MEM_DMAC;
    unsigned int inc_mode = B_CTLL_DINC_INC;
#else
    unsigned int dev_mode = B_CTLL_MEM2PER_DMAC;
    unsigned int inc_mode = B_CTLL_DINC_UNC;
#endif    
    unsigned int llp_mode = (dma_t->sg)   ?  (B_CTLL_LLP_DST_EN | B_CTLL_LLP_SRC_EN) : 0;
    unsigned int int_mode = (!dma_t->sg)   ?  B_CTLL_INT_EN : 0;
    
    unsigned int ctll = B_CTLL_SRC_TR_WIDTH_32 | B_CTLL_DST_TR_WIDTH(dev_info->fifo_width >> 4) |
               B_CTLL_SINC_INC | inc_mode |
               B_CTLL_DMS_ARMD | B_CTLL_SMS_EXP | dev_mode |
               B_CTLL_SRC_MSIZE_4 | B_CTLL_DST_MSIZE_4 |
               llp_mode | int_mode;
               
    return ctll;               
}

/**
 * rk28_dma_ctl_for_read - set dma control register for reading mode   
 *
 */
static inline unsigned int rk28_dma_ctl_for_read(unsigned int dma_ch, const struct rk28_dma_dev *dev_info, dma_t *dma_t)
{    
#ifdef test_dma
    unsigned int dev_mode = B_CTLL_MEM2MEM_DMAC;
    unsigned int inc_mode = B_CTLL_SINC_INC;
#else
    unsigned int dev_mode = B_CTLL_PER2MEM_DMAC;
    unsigned int inc_mode = B_CTLL_SINC_UNC;
#endif
    unsigned int llp_mode = (dma_t->sg)   ?  (B_CTLL_LLP_DST_EN | B_CTLL_LLP_SRC_EN) : 0;
    unsigned int int_mode = (!dma_t->sg)   ?  B_CTLL_INT_EN : 0;
    
    unsigned int ctll = B_CTLL_SRC_TR_WIDTH(dev_info->fifo_width>> 4) | B_CTLL_DST_TR_WIDTH_32 |
               inc_mode | B_CTLL_DINC_INC |
               B_CTLL_DMS_EXP | B_CTLL_SMS_ARMD | dev_mode |
               B_CTLL_SRC_MSIZE_4 | B_CTLL_DST_MSIZE_4 | 
               llp_mode | int_mode;
               
    return ctll;               
}

/**
 * rk28_dma_set_reg - set dma registers  
 *
 */
static inline void rk28_dma_set_reg(unsigned int dma_ch, struct rk28_dma_llp *reg, unsigned int dma_if)
{    
    write_dma_reg(DWDMA_SAR(dma_ch), reg->sar); 
    write_dma_reg(DWDMA_DAR(dma_ch), reg->dar);         
    write_dma_reg(DWDMA_LLP(dma_ch), (unsigned int)(reg->llp));        
    write_dma_reg(DWDMA_CTLL(dma_ch), reg->ctll);
    write_dma_reg(DWDMA_CTLH(dma_ch), reg->size);
    write_dma_reg(DWDMA_CFGL(dma_ch),  B_CFGL_CH_PRIOR(7) | 
                                                B_CFGL_H_SEL_DST | B_CFGL_H_SEL_SRC |
                                                B_CFGL_DST_HS_POL_H | B_CFGL_SRC_HS_POL_H);
    write_dma_reg(DWDMA_CFGH(dma_ch), B_CFGH_SRC_PER(dma_if & 0xf) | 
                                               B_CFGH_DST_PER(dma_if & 0xf) |
                                               B_CFGH_PROTCTL);
}

/**
 * rk28_dma_setup_reg - set linked list content  
 *
 */
static inline void rk28_dma_set_llp(unsigned int sar, 
                                         unsigned int dar, 
                                         struct rk28_dma_llp *curllp, 
                                         struct rk28_dma_llp *nexllp, 
                                         unsigned int ctll,
                                         unsigned int size)
{            
    curllp->sar  = sar; //pa
    curllp->dar  = dar; //pa
    curllp->ctll = ctll;	
    curllp->llp  = nexllp; //physical next linked list pointer
    curllp->size = size;
}

/**
 * rk28_dma_end_of_llp - set linked list end  
 *
 */
static inline void rk28_dma_end_of_llp(struct rk28_dma_llp *curllp)
{
    curllp->llp = 0; 
    curllp->ctll &= (~B_CTLL_LLP_DST_EN) & (~B_CTLL_LLP_SRC_EN);
    curllp->ctll |= B_CTLL_INT_EN;
}

/**
 * rk28_dma_setup_sg - setup rk28 DMA channel SG list to/from device transfer
 * @dma_ch: rk28 device ID which using DMA, device id list is showed in dma.h
 * @dma_t: pointer to the dma struct
 *
 * The function sets up DMA channel state and registers to be ready for transfer
 * specified by provided parameters. The scatter-gather emulation is set up
 * according to the parameters.
 *
 * enbale dma should be called after setup sg
 *
 * Return value: negative if incorrect parameters
 * Zero indicates success.
 */
static void rk28_dma_write_to_sg(unsigned int dma_ch, dma_t *dma_t)
{
    unsigned int i, ctll_r, dev_addr_w;
    struct rk2818_dma *rk28dma;
    struct rk28_dma_llp * rk28llp_vir;
    struct rk28_dma_llp * rk28llp_phy;
    struct rk28_dma_llp rk28dma_reg;
    struct scatterlist *sg;
	unsigned int wid_off, bk_count, bk_res, sgcount_tmp, bk_length;
	
    rk28dma = &rk2818_dma[dma_ch];
    	
    dev_addr_w = rk28dma->dev_info->dev_addr_w;
    ctll_r = rk28_dma_ctl_for_read(dma_ch, rk28dma->dev_info, dma_t);
    wid_off = rk28dma->dev_info->fifo_width >> 4;

    if (dma_t->sg) {
        rk28llp_vir = rk28dma->dma_llp_vir;
        rk28llp_phy = (struct rk28_dma_llp *)rk28dma->dma_llp_phy;
        sg = dma_t->sg;
#if 1
        bk_length = RK28_DMA_CH0A1_MAX_LEN << wid_off;
        
        for (sgcount_tmp = 0; sgcount_tmp < dma_t->sgcount; sgcount_tmp++, sg++) { 
            bk_count = (sg->length >> wid_off) / RK28_DMA_CH0A1_MAX_LEN;
            bk_res = (sg->length >> wid_off) % RK28_DMA_CH0A1_MAX_LEN;
            for (i = 0; i < bk_count; i++) { 
                rk28_dma_set_llp(dev_addr_w,
                               sg->dma_address + i * bk_length, 
                               rk28llp_vir++,
                               ++rk28llp_phy,
                               ctll_r,
                               RK28_DMA_CH0A1_MAX_LEN);
            }
            if (bk_res > 0) {
                rk28_dma_set_llp(dev_addr_w,
                               sg->dma_address + bk_count * bk_length,
                               rk28llp_vir++,
                               ++rk28llp_phy,
                               ctll_r,
                               bk_res);
            }
        }
#else
        for (i = 0; i < dma_t->sgcount; i++, sg++) { 
            rk28_dma_set_llp(dev_addr_w, sg->dma_address, rk28llp_vir++, ++rk28llp_phy, ctll_r, (sg->length >> wid_off));
        }
#endif        
        rk28_dma_end_of_llp(rk28llp_vir - 1);
        rk28dma_reg.llp = (struct rk28_dma_llp *)rk28dma->dma_llp_phy;
    } else { /*single transfer*/
        if (dma_t->buf.length > RK28_DMA_CH2_MAX_LEN) {
            rk28dma->length = RK28_DMA_CH2_MAX_LEN;
            rk28dma->residue = dma_t->buf.length - RK28_DMA_CH2_MAX_LEN;
        } else {
            rk28dma->length = dma_t->buf.length;
            rk28dma->residue = 0;
        }
        rk28dma_reg.llp = NULL;
    }
    rk28dma_reg.sar = dev_addr_w;
    rk28dma_reg.dar = dma_t->buf.dma_address;
    rk28dma_reg.ctll = ctll_r;
    rk28dma_reg.size = rk28dma->length;
    rk28_dma_set_reg(dma_ch, &rk28dma_reg, rk28dma->dev_info->hd_if_r);
    /*
    printk(KERN_INFO "dma_write_to_sg: ch = %d, sar = 0x%x, dar = 0x%x, ctll = 0x%x, llp = 0x%x, size = %d, \n", 
                     dma_ch, rk28dma_reg.sar, rk28dma_reg.dar, rk28dma_reg.ctll, rk28dma_reg.llp, rk28dma_reg.size);
    rk28llp_vir = rk28dma->dma_llp_vir;
    for (i=0; i<dma_t->sgcount; i++, rk28llp_vir++) 
    printk(KERN_INFO "dma_write_to_sg: ch = %d, sar = 0x%x, dar = 0x%x, ctll = 0x%x, llp = 0x%x, size = %d, \n", 
                     dma_ch, rk28llp_vir->sar, rk28llp_vir->dar, rk28llp_vir->ctll, rk28llp_vir->llp, rk28llp_vir->size);
    */

}

/**
 * rk28_dma_setup_sg - setup rk28 DMA channel SG list to/from device transfer
 * @dma_ch: rk28 device ID which using DMA, device id list is showed in dma.h
 * @dma_t: pointer to the dma struct
 *
 * The function sets up DMA channel state and registers to be ready for transfer
 * specified by provided parameters. The scatter-gather emulation is set up
 * according to the parameters.
 *
 * enbale dma should be called after setup sg
 *
 * Return value: negative if incorrect parameters
 * Zero indicates success.
 */
static void rk28_dma_read_from_sg(unsigned int dma_ch, dma_t *dma_t)
{
    unsigned int i, ctll_w, dev_addr_r;
    struct rk2818_dma *rk28dma;
    struct rk28_dma_llp * rk28llp_vir;
    struct rk28_dma_llp * rk28llp_phy;
    struct rk28_dma_llp rk28dma_reg;
    struct scatterlist *sg;
	unsigned int wid_off, bk_count, bk_res, sgcount_tmp, bk_length;

    rk28dma = &rk2818_dma[dma_ch];
        
    /*setup linked list table end*/
    dev_addr_r = rk28dma->dev_info->dev_addr_r;
    ctll_w = rk28_dma_ctl_for_write(dma_ch, rk28dma->dev_info, dma_t);
    wid_off = rk28dma->dev_info->fifo_width >> 4;
        
    if (dma_t->sg) {
        rk28llp_vir = rk28dma->dma_llp_vir;
        rk28llp_phy = (struct rk28_dma_llp *)rk28dma->dma_llp_phy;
        sg = dma_t->sg;
#if 1
        bk_length = RK28_DMA_CH0A1_MAX_LEN << wid_off;
        
        for (sgcount_tmp = 0; sgcount_tmp < dma_t->sgcount; sgcount_tmp++, sg++) { 
            bk_count = (sg->length >> wid_off) / RK28_DMA_CH0A1_MAX_LEN;
            bk_res = (sg->length >> wid_off) % RK28_DMA_CH0A1_MAX_LEN;
            for (i = 0; i < bk_count; i++) { 
                rk28_dma_set_llp(sg->dma_address + i * bk_length, 
                               dev_addr_r,
                               rk28llp_vir++,
                               ++rk28llp_phy,
                               ctll_w,
                               RK28_DMA_CH0A1_MAX_LEN);
            }
            if (bk_res > 0) {
                rk28_dma_set_llp(sg->dma_address + bk_count * bk_length,
                               dev_addr_r,
                               rk28llp_vir++,
                               ++rk28llp_phy,
                               ctll_w,
                               bk_res);
            }
        }
#else
        for (i = 0; i < dma_t->sgcount; i++, sg++) { 
            rk28_dma_set_llp(sg->dma_address, dev_addr_r, rk28llp_vir++, ++rk28llp_phy, ctll_w, (sg->length >> wid_off));
        }
#endif        
        rk28_dma_end_of_llp(rk28llp_vir - 1);
        rk28dma_reg.llp = (struct rk28_dma_llp *)rk28dma->dma_llp_phy;
    } else { /*single transfer*/
        if (dma_t->buf.length > RK28_DMA_CH2_MAX_LEN) {
            rk28dma->length = RK28_DMA_CH2_MAX_LEN;
            rk28dma->residue = dma_t->buf.length - RK28_DMA_CH2_MAX_LEN;
        } else {
            rk28dma->length = dma_t->buf.length;
            rk28dma->residue = 0;
        } 
        rk28dma_reg.llp = NULL;
    }
    rk28dma_reg.sar = dma_t->buf.dma_address;
    rk28dma_reg.dar = dev_addr_r;
    rk28dma_reg.ctll = ctll_w;
    rk28dma_reg.size = rk28dma->length;
    rk28_dma_set_reg(dma_ch, &rk28dma_reg, rk28dma->dev_info->hd_if_w);

    //printk(KERN_INFO "read_from_sg: ch = %d, sar = 0x%x, dar = 0x%x, ctll = 0x%x, llp = 0x%x, size = %d, \n", 
    //                 dma_ch, rk28dma_reg.sar, rk28dma_reg.dar, rk28dma_reg.ctll, rk28dma_reg.llp, rk28dma_reg.size);
}

/**
 * rk28_dma_enable - function to start rk28 DMA channel operation
 * @dma_ch: rk28 device ID which using DMA, device id list is showed in dma.h
 *
 * The channel has to be allocated by driver through rk28_dma_request()
 * The transfer parameters has to be set to the channel registers through
 * call of the rk28_dma_setup_sg() function.

 */
static int rk28_dma_enable(unsigned int dma_ch, dma_t *dma_t)
{	
    //printk(KERN_INFO "enter dwdma_enable\n");
	struct rk2818_dma *rk28dma = &rk2818_dma[dma_ch];
    
	spin_lock(&rk28dma->lock);
	
    if (dma_t->sg) {
        if (dma_ch >= RK28_DMA_CH2) {
            printk(KERN_ERR "dma_enable: channel %d does not support sg transfer mode\n", dma_ch);
            goto bad_enable;
        }
        if (dma_t->sgcount > RK28_MAX_DMA_LLPS) {
            printk(KERN_ERR "dma_enable: count %d are more than supported number %d\n", dma_t->sgcount, RK28_MAX_DMA_LLPS);
            goto bad_enable;
        }        
    } else { /*single transfer*/
        if ((!dma_t->addr) || (dma_t->count == 0)) {
            printk(KERN_ERR "dma_enable: channel %d does not have leagal address or count\n", dma_ch);
            goto bad_enable;
        }    	
        dma_t->buf.dma_address = (dma_addr_t)dma_t->addr;
        dma_t->buf.length = dma_t->count;
    }
    //printk(KERN_INFO "dma_enable:  addr = 0x%x\n", (dma_addr_t)dma_t->addr);

    if (dma_t->dma_mode == DMA_MODE_READ) {
        rk28_dma_write_to_sg(dma_ch, dma_t);
    } else {
        rk28_dma_read_from_sg(dma_ch, dma_t);
    }
    
    dma_t->invalid = 0;
    
	ENABLE_DWDMA(dma_ch);
	
	spin_unlock(&rk28dma->lock);
    //printk(KERN_INFO "exit dwdma_enable\n");
	
    return 0;	

bad_enable:
    dma_t->active = 0;
    return -EINVAL;
}

/**
 * rk28_dma_disable - stop, finish rk28 DMA channel operatin
 * @dma_ch: rk28 device ID which using DMA, device id list is showed in dma.h
 *
 * dma transfer will be force into suspend state whether dma have completed current transfer
 */
static int rk28_dma_disable(unsigned int dma_ch, dma_t *dma_t)
{	
	struct rk2818_dma *rk28dma = &rk2818_dma[dma_ch];
	
	spin_lock(&rk28dma->lock);
	
	DISABLE_DWDMA(dma_ch);
	while (GET_DWDMA_STATUS(dma_ch))
		cpu_relax();
		
    rk28dma->tasklet_flag = 0;
    
	spin_unlock(&rk28dma->lock);
	
    return 0;	
}

/**
 * rk28_dma_request - request/allocate specified channel number
 * @dma_ch: rk28 device ID which using DMA, device id list is showed in dma.h
 * requesting dma channel if device need dma transfer 
 * but just called one time in one event, and channle should be 
 * free after this event  
 */
static int rk28_dma_request(unsigned int dma_ch, dma_t *dma_t)
{  
    int i;
	struct rk2818_dma *rk28dma = &rk2818_dma[dma_ch];

    //printk(KERN_INFO "enter dwdma request\n");
    
	spin_lock(&rk28dma->lock);

    /*compare to make sure whether device that request dma is legal*/
    for (i = 0; i < RK28_DMA_DEV_NUM_MAX; i++) {
        if (!strcmp(dma_t->device_id, rk28_dma_dev_id[i]))
            break;
    }

    if (i >= RK28_DMA_DEV_NUM_MAX) {
		printk(KERN_ERR "dma_request: called for  non-existed dev %s\n", dma_t->device_id);
		return -ENODEV;
    }
    
	/*channel 0 and 1 support llp, but others does not*/    
	if (dma_ch < RK28_DMA_CH2) { 
        rk28dma->dma_llp_vir = (struct rk28_dma_llp *)dma_alloc_coherent(NULL, RK28_MAX_DMA_LLPS*sizeof(struct rk28_dma_llp), &rk28dma->dma_llp_phy, GFP_ATOMIC);
        if (!rk28dma->dma_llp_vir) {
            printk(KERN_ERR "dma_request: no dma space can be allocated for llp by virtual channel %d\n", dma_ch); 
            return -ENOMEM;
        }
    } else {
        rk28dma->dma_llp_vir = NULL;
        rk28dma->dma_llp_phy = 0;
    }
    
    rk28dma->dev_info = &rk28_dev_info[i];
	
	/* clear interrupt */
    CLR_DWDMA_INTR(dma_ch);
    
	UN_MASK_DWDMA_TRF_INTR(dma_ch);

	spin_unlock(&rk28dma->lock);

    //printk(KERN_INFO "exit dwdma request device %d\n", i);

	return 0;
}

/**
 * rk28_dma_free - release previously acquired channel
 * @dma_ch: rk28 device ID which using DMA, device id list is showed in dma.h
 *
 * request dam should be prior free dma
 */
static int rk28_dma_free(unsigned int dma_ch, dma_t *dma_t)
{    
	struct rk2818_dma *rk28dma = &rk2818_dma[dma_ch];

	spin_lock(&rk28dma->lock);
	
	/* clear interrupt */
    CLR_DWDMA_INTR(dma_ch);
    
	MASK_DWDMA_TRF_INTR(dma_ch);

	if (dma_ch < RK28_DMA_CH2) {
        if (!rk28dma->dma_llp_vir) {
            printk(KERN_ERR "dma_free: no dma space can be free by virtual channel %d\n", dma_ch);  
            return -ENOMEM;       
        }
        dma_free_coherent(NULL, RK28_MAX_DMA_LLPS*sizeof(struct rk28_dma_llp), (void *)rk28dma->dma_llp_vir, rk28dma->dma_llp_phy);
    }
    
    rk28dma->dma_t.irqHandle = NULL;
    rk28dma->dma_t.data = NULL;
    rk28dma->dma_t.sg = NULL;
    rk28dma->dma_t.addr = NULL;
    rk28dma->dma_t.count = 0;      
    rk28dma->dma_llp_vir = NULL;
    rk28dma->dma_llp_phy = 0;
    rk28dma->residue = 0;
    rk28dma->length = 0;

	spin_unlock(&rk28dma->lock);
	
	return 0;
}

/**
 * rk28_dma_next - set dma regiters and start of next transfer if using channel 2   
 * @dma_ch: rk28 device ID which using DMA, device id list is showed in dma.h
 *
 * just be applied to channel 2
 */
static int rk28_dma_next(unsigned int dma_ch)
{
	struct rk2818_dma *rk28dma = &rk2818_dma[dma_ch];	
	unsigned int nextlength;
	unsigned int nextaddr;
    unsigned int width_off = rk28dma->dev_info->fifo_width >> 4;
    unsigned int dma_if;
    struct rk28_dma_llp rk28dma_reg;

	/*go on transfering if there are buffer of other blocks leave*/
	if (rk28dma->residue > 0) {
        nextaddr = rk28dma->dma_t.buf.dma_address + (rk28dma->length << width_off);
        if (rk28dma->residue > RK28_DMA_CH2_MAX_LEN) {
            nextlength = RK28_DMA_CH2_MAX_LEN;
            rk28dma->residue -= RK28_DMA_CH2_MAX_LEN; 
        } else {
            nextlength = rk28dma->residue;
            rk28dma->residue = 0; 
        }
        rk28dma->length = nextlength;
        
        if (rk28dma->dma_t.dma_mode == DMA_MODE_READ) {
            rk28dma_reg.sar = rk28dma->dev_info->dev_addr_r;
            rk28dma_reg.dar = nextaddr;
            rk28dma_reg.ctll = rk28_dma_ctl_for_read(dma_ch, rk28dma->dev_info, &rk28dma->dma_t);
            dma_if = rk28dma->dev_info->hd_if_r;
        } else {
            rk28dma_reg.sar = nextaddr;
            rk28dma_reg.dar = rk28dma->dev_info->dev_addr_w;
            rk28dma_reg.ctll = rk28_dma_ctl_for_write(dma_ch, rk28dma->dev_info, &rk28dma->dma_t);
            dma_if = rk28dma->dev_info->hd_if_w;
        }
        rk28dma_reg.llp = NULL;
        rk28dma_reg.size = nextlength;
        
        rk28_dma_set_reg(dma_ch, &rk28dma_reg, dma_if);
        
        ENABLE_DWDMA(dma_ch);
        
        return nextlength;
	} 

	return 0;
}

/**
 * rk28_dma_tasklet - irq callback function   
 *
 */
static void rk28_dma_tasklet(unsigned long data)
{
	int i;
    struct rk2818_dma *rk28dma;

    for (i = 0; i < MAX_DMA_CHANNELS; i++) {
        rk28dma = &rk2818_dma[i];
        if ((rk28dma->tasklet_flag) && (rk28dma->dma_t.irq_mode == DMA_IRQ_DELAY_MODE)) {
            rk28dma->dma_t.active = 0;
            rk28dma->tasklet_flag = 0;
            if (rk28dma->dma_t.irqHandle)
                rk28dma->dma_t.irqHandle(i, rk28dma->dma_t.data);
            UN_MASK_DWDMA_TRF_INTR(i);
        }
    }
}

/**
 * rk28_dma_irq_handler - irq callback function   
 *
 */
static irqreturn_t rk28_dma_irq_handler(int irq, void *dev_id)
{
	int i, raw_status;
    struct rk2818_dma *rk28dma;

    raw_status = read_dma_reg(DWDMA_RawTfr);
    
	for (i = 0; i < MAX_DMA_CHANNELS; i++) {
        if (raw_status & (1 << i)) {
            CLR_DWDMA_INTR(i);
            
            rk28dma = &rk2818_dma[i];
            
            if ((!rk28dma->dma_t.sg) && (rk28_dma_next(i))) {
                //printk(KERN_WARNING "dma_irq: don't finish  for channel %d\n", i);
                continue;
            }  

            if (rk28dma->dma_t.irqHandle) {
                if (rk28dma->dma_t.irq_mode != DMA_IRQ_DELAY_MODE) {
                    /* already have completed transfer */
                    rk28dma->dma_t.active = 0;
                    rk28dma->dma_t.irqHandle(i, rk28dma->dma_t.data);
                } else {
                    MASK_DWDMA_TRF_INTR(i);
                    rk28dma->tasklet_flag = 1;
                    tasklet_schedule(&rk2818_dma_tasklet);
                    //printk(KERN_WARNING "dma_irq: no IRQ handler for DMA channel %d\n", i);
                }
            } else {
                /* already have completed transfer */
                rk28dma->dma_t.active = 0;
            }   
        }
	}
	
    //tasklet_schedule(&rk2818_dma_tasklet);

	return IRQ_HANDLED;
}

static void rk28_dma_position(unsigned int dma_ch, dma_t *dma_t)
{
    dma_t->src_pos = read_dma_reg(DWDMA_SAR(dma_ch));
    dma_t->dst_pos = read_dma_reg(DWDMA_DAR(dma_ch));
}

static struct dma_ops rk2818_dma_ops = {
    .request = rk28_dma_request,
    .free = rk28_dma_free,
    .enable = rk28_dma_enable,
    .disable = rk28_dma_disable,
    .position = rk28_dma_position,
};

/**
 * rk28_dma_init - dma information initialize   
 *
 */
static int __init rk28_dma_init(void)
{
	int ret;
	int i;

    printk(KERN_INFO "enter dwdma init\n");

	for (i = 0; i < MAX_DMA_CHANNELS; i++) {
		rk2818_dma[i].dma_t.irqHandle = NULL;
		rk2818_dma[i].dma_t.data = NULL;
		rk2818_dma[i].dma_t.sg = NULL;
		rk2818_dma[i].dma_t.addr = NULL;
		rk2818_dma[i].dma_t.count = 0;		
		rk2818_dma[i].dma_t.d_ops = &rk2818_dma_ops;
	    dma_add(i, &rk2818_dma[i].dma_t);

        rk2818_dma[i].dma_llp_vir = NULL;
        rk2818_dma[i].dma_llp_phy = 0;
        rk2818_dma[i].residue = 0;
        rk2818_dma[i].length = 0;
        rk2818_dma[i].tasklet_flag = 0;
		spin_lock_init(&rk2818_dma[i].lock);
	}
    	
	/* clear all interrupts */
	write_dma_reg(DWDMA_ClearBlock, 0x3f3f);
	write_dma_reg(DWDMA_ClearTfr, 0x3f3f);
	write_dma_reg(DWDMA_ClearSrcTran, 0x3f3f);
	write_dma_reg(DWDMA_ClearDstTran, 0x3f3f);
	write_dma_reg(DWDMA_ClearErr, 0x3f3f);

    /*mask all interrupts*/
	write_dma_reg(DWDMA_MaskBlock, 0x3f00);
	write_dma_reg(DWDMA_MaskSrcTran, 0x3f00);
	write_dma_reg(DWDMA_MaskDstTran, 0x3f00);
	write_dma_reg(DWDMA_MaskErr, 0x3f00);

    /*unmask transfer completion interrupt*/
	//UN_MASK_DWDMA_ALL_TRF_INTR;
	MASK_DWDMA_ALL_TRF_INTR;

	ret = request_irq(RK28_DMA_IRQ_NUM, rk28_dma_irq_handler, 0, "DMA", NULL);
	if (ret < 0) {
		printk(KERN_CRIT "Can't register IRQ for DMA\n");
		return ret;
	}

	tasklet_init(&rk2818_dma_tasklet, rk28_dma_tasklet, 0);

	/* enable DMA module */
	write_dma_reg(DWDMA_DmaCfgReg, 0x01);

    printk(KERN_INFO "exit dwdma init\n");
    
	return 0;
}
arch_initcall(rk28_dma_init);

MODULE_AUTHOR("nzy@rock-chips.com");
MODULE_DESCRIPTION("Driver for rk2818 dma device");
MODULE_LICENSE("GPL");
