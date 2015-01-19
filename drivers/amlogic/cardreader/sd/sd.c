/*****************************************************************
**                                                              **
**  Copyright (C) 2004 Amlogic,Inc.                             **
**  All rights reserved                                         **
**        Filename : sd.c /Project:AVOS  driver                 **
**        Revision : 1.0                                        **
**                                                              **
*****************************************************************/

#include <linux/err.h>
#include <linux/device.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <mach/am_regs.h>
#include <mach/irqs.h>
#include <mach/card_io.h>
#include <mach/mod_gate.h>
#include <linux/cardreader/card_block.h>
#include <linux/cardreader/cardreader.h>
#include <linux/cardreader/sdio.h>

#include "sd_misc.h"
#include "sd_protocol.h"

struct memory_card *card_find_card(struct card_host *host, u8 card_type); 

void sd_insert_detector(struct memory_card *card)
{
	SD_MMC_Card_Info_t *sd_mmc_info = (SD_MMC_Card_Info_t *)card->card_info;

	int ret = sd_mmc_check_insert(sd_mmc_info);
	if(ret)
        card->card_status = CARD_INSERTED;
    else
        card->card_status = CARD_REMOVED;

	return;
}

void sd_open(struct memory_card *card)
{
	int ret;
	struct aml_card_info *aml_card_info = card->card_plat_info;
	SD_MMC_Card_Info_t *sd_mmc_info = (SD_MMC_Card_Info_t *)card->card_info;

#if MESON_CPU_TYPE == MESON_CPU_TYPE_MESON6
	switch_mod_gate_by_type(MOD_SDIO,1);   
#endif

	if (aml_card_info->card_extern_init)
		aml_card_info->card_extern_init();
	ret = sd_mmc_init(sd_mmc_info);
	
	if(ret)
		ret = sd_mmc_init(sd_mmc_info);

	if(ret)
		ret = sd_mmc_init(sd_mmc_info);
		
	card->capacity = sd_mmc_info->blk_nums;
	card->sdio_funcs  = sd_mmc_info->sdio_function_nums;
	memcpy(card->raw_cid, &(sd_mmc_info->raw_cid), sizeof(card->raw_cid));

      if(sd_mmc_info->write_protected_flag)
            card->state |= CARD_STATE_READONLY;
      
#if MESON_CPU_TYPE == MESON_CPU_TYPE_MESON6
	switch_mod_gate_by_type(MOD_SDIO,0); 
#endif

	if(ret)
		card->unit_state = CARD_UNIT_READY;
	else
		card->unit_state = CARD_UNIT_PROCESSED;

	return;
}

void sd_close(struct memory_card *card)
{
	SD_MMC_Card_Info_t *sd_mmc_info = (SD_MMC_Card_Info_t *)card->card_info;

	if(!sd_mmc_info)
	{
		printk("error: no card to exit\n");
		return;
	}
	sd_mmc_exit(sd_mmc_info);
	sd_mmc_free(sd_mmc_info);
	sd_mmc_info = NULL;
	card->card_info = NULL;
	card->unit_state =  CARD_UNIT_PROCESSED;

	return;
}

void sd_suspend(struct memory_card *card)
{
	struct aml_card_info *aml_card_info = card->card_plat_info;
	struct card_host *host = card->host;
	
	SD_MMC_Card_Info_t *sd_mmc_info = (SD_MMC_Card_Info_t *)card->card_info;
    unsigned int power_delay = sd_mmc_info->sd_mmc_power_delay;

	printk("***Entered %s:%s\n", __FILE__,__func__);	
	
	__card_claim_host(host, card);
	 
	if(card->card_type == CARD_SDIO)
	{
		card_release_host(host);
		return;
	}
	
	card->card_io_init(card);
	sd_mmc_power_off(sd_mmc_info);
		        
	memset(sd_mmc_info, 0, sizeof(SD_MMC_Card_Info_t));
	if (card->host->dma_buf != NULL) {
		sd_mmc_info->sd_mmc_buf = card->host->dma_buf;
		sd_mmc_info->sd_mmc_phy_buf = card->host->dma_phy_buf;
	}	
	sd_mmc_info->io_pad_type = aml_card_info->io_pad_type;
	sd_mmc_info->bus_width = SD_BUS_SINGLE;
	sd_mmc_info->sdio_clk_unit = 3000;
	sd_mmc_info->clks_nac = SD_MMC_TIME_NAC_DEFAULT;
	sd_mmc_info->max_blk_count = card->host->max_blk_count;
    sd_mmc_info->sd_mmc_power_delay = power_delay;
	
	card_release_host(host);
	
}

void sd_resume(struct memory_card *card)
{
	printk("***Entered %s:%s\n", __FILE__,__func__);
}


static int sd_request(struct memory_card *card, struct card_blk_request *brq)
{
	SD_MMC_Card_Info_t *sd_mmc_info = (SD_MMC_Card_Info_t *)card->card_info;
	unsigned int lba, byte_cnt,ret;
	unsigned char *data_buf;
	struct card_host *host = card->host;
	struct memory_card *sdio_card;
	SD_MMC_Card_Info_t *sdio_info;
	
	lba = brq->card_data.lba;
	byte_cnt = brq->card_data.blk_size * brq->card_data.blk_nums;
	data_buf = brq->crq.buf;

	if(sd_mmc_info == NULL){
		brq->card_data.error = SD_MMC_ERROR_NO_CARD_INS;
		printk("[sd_request] sd_mmc_info == NULL, return SD_MMC_ERROR_NO_CARD_INS\n");
		return 0;
	}
	
	if(!sd_mmc_info->blk_len){
		card->card_io_init(card);
		card->card_detector(card);
      
		if(card->card_status == CARD_REMOVED){
			brq->card_data.error = SD_MMC_ERROR_NO_CARD_INS;
			return 0;      	
		}
		
#if MESON_CPU_TYPE == MESON_CPU_TYPE_MESON6
		switch_mod_gate_by_type(MOD_SDIO,1);  
#endif		
		ret = sd_mmc_init(sd_mmc_info); 
		
#if MESON_CPU_TYPE == MESON_CPU_TYPE_MESON6
		switch_mod_gate_by_type(MOD_SDIO,0);  
#endif			
		if(ret){	
			brq->card_data.error = SD_MMC_ERROR_NO_CARD_INS;
			return 0;
		}
    }	

#if MESON_CPU_TYPE == MESON_CPU_TYPE_MESON6
	switch_mod_gate_by_type(MOD_SDIO,1); 
#endif	
	
	sdio_card = card_find_card(host, CARD_SDIO);
	if (sdio_card) {
		sdio_close_host_interrupt(SDIO_IF_INT);
		sdio_info = (SD_MMC_Card_Info_t *)sdio_card->card_info;
		sd_gpio_enable(sdio_info->io_pad_type);
	}
	
	sd_sdio_enable(sd_mmc_info->io_pad_type);
	if(brq->crq.cmd == READ) {
		brq->card_data.error = sd_mmc_read_data(sd_mmc_info, lba, byte_cnt, data_buf);
	}
	else if(brq->crq.cmd == WRITE) {
		brq->card_data.error = sd_mmc_write_data(sd_mmc_info, lba, byte_cnt, data_buf);
	}
	sd_gpio_enable(sd_mmc_info->io_pad_type);

	if(brq->card_data.error == SD_WAIT_FOR_COMPLETION_TIMEOUT)
        {
                printk("[sd_request] wait for completion timeout, reinit\n");
                card->card_io_init(card);
                card->card_detector(card);

                if(card->card_status == CARD_REMOVED){
			printk("[sd_request] card removed\n");
                        brq->card_data.error = SD_MMC_ERROR_NO_CARD_INS;
                        return 0;
                }
                sd_mmc_staff_init(sd_mmc_info);
                ret = sd_mmc_init(sd_mmc_info);
                if(ret){
                        printk("[sd_request] reinit fail %d\n", ret);
                        brq->card_data.error = SD_MMC_ERROR_NO_CARD_INS;
                        return 0;
                }

                sd_sdio_enable(sd_mmc_info->io_pad_type);
                if(brq->crq.cmd == READ) {
                        brq->card_data.error = sd_mmc_read_data(sd_mmc_info, lba, byte_cnt, data_buf);
                }
                else if(brq->crq.cmd == WRITE) {
                        brq->card_data.error = sd_mmc_write_data(sd_mmc_info, lba, byte_cnt, data_buf);
                }
                sd_gpio_enable(sd_mmc_info->io_pad_type);
                if(brq->card_data.error == SD_WAIT_FOR_COMPLETION_TIMEOUT)
                        printk("[sd_request] after reinit still error \n");
        }

	sdio_card = card_find_card(host, CARD_SDIO);
	if(sdio_card) {
		sdio_info = (SD_MMC_Card_Info_t *)sdio_card->card_info;
		sd_sdio_enable(sdio_info->io_pad_type);
		if (sdio_info->sd_save_hw_io_flag) {
	    		WRITE_CBUS_REG(SDIO_CONFIG, sdio_info->sd_save_hw_io_config);
	      		WRITE_CBUS_REG(SDIO_MULT_CONFIG, sdio_info->sd_save_hw_io_mult_config);
    	}
		sdio_open_host_interrupt(SDIO_IF_INT);
	}
	
#if MESON_CPU_TYPE == MESON_CPU_TYPE_MESON6
	switch_mod_gate_by_type(MOD_SDIO,0);
#endif	

	return 0;
}

static int sdio_request(struct memory_card *card, struct card_blk_request *brq)
{
	SD_MMC_Card_Info_t *sdio_info = (SD_MMC_Card_Info_t *)card->card_info;
	int incr_addr, err;
	unsigned addr, blocks, blksz, fn, read_after_write;
	u8 *in, *out, *buf;

#if MESON_CPU_TYPE == MESON_CPU_TYPE_MESON6
	switch_mod_gate_by_type(MOD_SDIO,1);   
#endif	
	//set_cpus_allowed_ptr(current, cpumask_of(0));
	sd_sdio_enable(sdio_info->io_pad_type);
	if (brq->crq.cmd & SDIO_OPS_REG) {

		WARN_ON(brq->card_data.blk_size != 1);
		WARN_ON(brq->card_data.blk_nums != 1);
	
		in = brq->crq.buf;
		addr = brq->card_data.lba;
		fn = brq->card_data.flags;
		out = brq->crq.back_buf;

		if (brq->crq.cmd & READ_AFTER_WRITE)
			read_after_write = 1;
		else
			read_after_write = 0;

		if ((brq->crq.cmd & 0x1 )== WRITE) {
			err = sdio_write_reg(sdio_info, fn, addr, in, read_after_write);
			if (err) {
				printk("sdio card write_reg failed %d at addr: %x \n", err, addr);
				brq->card_data.error = err;
				goto err;
			}
		}
		else {
			err = sdio_read_reg(sdio_info, fn, addr, out);
			if (err) {
				printk("sdio card read_reg failed %d at addr: %x  \n", err, addr);
				brq->card_data.error = err;
				goto err;
			}
		}
	}
	else {

		if (brq->crq.cmd & SDIO_FIFO_ADDR)
			incr_addr = 1;
		else
			incr_addr = 0;

		buf = brq->crq.buf;
		addr = brq->card_data.lba;
		blksz = brq->card_data.blk_size;
		blocks = brq->card_data.blk_nums;
		fn = brq->card_data.flags;
		sdio_info->sdio_blk_len[fn] = card->sdio_func[fn-1]->cur_blksize;

		if ((brq->crq.cmd & 0x1)== WRITE) {
			err = sdio_write_data(sdio_info, fn, incr_addr, addr, blocks*blksz, buf);
			if (err) {
				printk("sdio card write_data failed %d at addr: %x, function: %d \n", err, addr, fn);
				brq->card_data.error = err;
				goto err;
			}
		}
		else {
			err = sdio_read_data(sdio_info, fn, incr_addr, addr, blocks*blksz, buf);
			if (err) {
				printk("sdio card read_data failed %d at addr: %x, function: %d\n", err, addr, fn);
				brq->card_data.error = err;
				goto err;
			}
		}
	}

	//sd_gpio_enable(sdio_info->io_pad_type);
	brq->card_data.error = 0;
#if MESON_CPU_TYPE == MESON_CPU_TYPE_MESON6
	switch_mod_gate_by_type(MOD_SDIO,0);  
#endif		
	return 0;

err:
	
#if MESON_CPU_TYPE == MESON_CPU_TYPE_MESON6
	switch_mod_gate_by_type(MOD_SDIO,0);  
#endif		
	//sd_gpio_enable(sdio_info->io_pad_type);
	return err;
}

int sd_mmc_probe(struct memory_card *card)
{
	struct aml_card_info *aml_card_info = card->card_plat_info;

	SD_MMC_Card_Info_t *sd_mmc_info = sd_mmc_malloc(sizeof(SD_MMC_Card_Info_t), GFP_KERNEL);
	if (sd_mmc_info == NULL)
		return -ENOMEM;

	if (card->host->dma_buf != NULL) {
		sd_mmc_info->sd_mmc_buf = card->host->dma_buf;
		sd_mmc_info->sd_mmc_phy_buf = card->host->dma_phy_buf;
	}

	card->card_info = sd_mmc_info;
	card->card_io_init = sd_io_init;
	card->card_detector = sd_insert_detector;
	card->card_insert_process = sd_open;
	card->card_remove_process = sd_close;
	card->card_request_process = sd_request;
	card->card_suspend = sd_suspend;
	card->card_resume = sd_resume;
	if (aml_card_info->card_extern_init)
		aml_card_info->card_extern_init();
	card->card_io_init(card);
	sd_mmc_prepare_init(sd_mmc_info);
	sd_mmc_info->io_pad_type = aml_card_info->io_pad_type;
	sd_mmc_info->bus_width = SD_BUS_SINGLE;
	sd_mmc_info->sdio_clk_unit = 3000;
	sd_mmc_info->clks_nac = SD_MMC_TIME_NAC_DEFAULT;
	sd_mmc_info->max_blk_count = card->host->max_blk_count;
    
    sd_mmc_info->sd_mmc_power_delay = 200;
#ifdef CONFIG_CARD_NO_POWER_DELAY
    if(!aml_card_info->card_power_en_reg)
        sd_mmc_info->sd_mmc_power_delay = 0;
#endif


	return 0;
}

int sdio_probe(struct memory_card *card)
{
	struct aml_card_info *aml_card_info = card->card_plat_info;

	SD_MMC_Card_Info_t *sdio_info = sd_mmc_malloc(sizeof(SD_MMC_Card_Info_t), GFP_KERNEL);
	if (sdio_info == NULL)
		return -ENOMEM;

	if (card->host->dma_buf != NULL) {
		sdio_info->sd_mmc_buf = card->host->dma_buf;
		sdio_info->sd_mmc_phy_buf = card->host->dma_phy_buf;
	}

	card->card_info = sdio_info;
	card->card_io_init = sd_io_init;
	card->card_detector = sd_insert_detector;
	card->card_insert_process = sd_open;
	card->card_remove_process = sd_close;
	card->card_request_process = sdio_request;
        card->card_suspend = sd_suspend;
        card->card_resume = sd_resume;
	if (aml_card_info->card_extern_init)
		aml_card_info->card_extern_init();
	card->card_io_init(card);
	sd_mmc_prepare_init(sdio_info);
	sdio_info->io_pad_type = aml_card_info->io_pad_type;
	sdio_info->bus_width = SD_BUS_SINGLE;
	sdio_info->sdio_clk_unit = 3000;
	sdio_info->clks_nac = SD_MMC_TIME_NAC_DEFAULT;
	sdio_info->max_blk_count = card->host->max_blk_count;
    
    sdio_info->sd_mmc_power_delay = 200;
#ifdef CONFIG_CARD_NO_POWER_DELAY
    if(!aml_card_info->card_power_en_reg)
        sdio_info->sd_mmc_power_delay = 0;
#endif

	return 0;
}

#ifdef CONFIG_INAND



int inand_probe(struct memory_card *card)
{
	struct aml_card_info *aml_card_info = card->card_plat_info;

	SD_MMC_Card_Info_t *sdio_info = sd_mmc_malloc(sizeof(SD_MMC_Card_Info_t), GFP_KERNEL);
	if (sdio_info == NULL)
		return -ENOMEM;

	if (card->host->dma_buf != NULL) {
		sdio_info->sd_mmc_buf = card->host->dma_buf;
		sdio_info->sd_mmc_phy_buf = card->host->dma_phy_buf;
	}

	card->card_info = sdio_info;
	card->card_io_init = sd_io_init;
	card->card_detector = sd_insert_detector;
	card->card_insert_process = sd_open;
	card->card_remove_process = sd_close;
	card->card_request_process = sd_request;
	card->card_suspend = sd_suspend;
	card->card_resume = sd_resume;

	if (aml_card_info->card_extern_init)
		aml_card_info->card_extern_init();
	card->card_io_init(card);
	sd_mmc_prepare_init(sdio_info);
	sdio_info->io_pad_type = aml_card_info->io_pad_type;
	sdio_info->bus_width = SD_BUS_SINGLE;
	sdio_info->sdio_clk_unit = 3000;
	sdio_info->clks_nac = SD_MMC_TIME_NAC_DEFAULT;
	sdio_info->max_blk_count = card->host->max_blk_count;
    
    sdio_info->sd_mmc_power_delay = 200;
#ifdef CONFIG_CARD_NO_POWER_DELAY
    if(!aml_card_info->card_power_en_reg)
        sdio_info->sd_mmc_power_delay = 0;
#endif


#ifdef CONFIG_AML_CARD_KEY
	{
		int card_key_init(struct memory_card *card);
		card_key_init(card);
	}
#endif
	return 0;
}

#ifdef CONFIG_INAND_LP

static int sd_part_request(struct memory_card *card, 
                              struct card_blk_request *brq)
{
      int err;

      brq->card_data.lba += card->part_offset;
      WARN_ON(brq->card_data.lba+brq->card_data.blk_nums > card->capacity);
      err = sd_request(card, brq);
      return err;
}

int inand_lp_probe(struct memory_card *card)
{
      int err;
      
      err = inand_probe(card);
      card->card_request_process = sd_part_request;
      return err;
}

#endif
#endif

MODULE_DESCRIPTION("Amlogic sd card Interface driver");

MODULE_LICENSE("GPL");

