/*****************************************************************
**                                                              **
**  Copyright (C) 2004 Amlogic,Inc.                             **
**  All rights reserved                                         **
**        Filename : sd.c /Project:AVOS  driver                 **
**        Revision : 1.0                                        **
**                                                              **
*****************************************************************/
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/pagemap.h>
#include <linux/fs.h>

#include <linux/cardreader/card_block.h>
#include <linux/cardreader/cardreader.h>
#include "ms_misc.h"

extern unsigned ms_force_write_two_times;


static unsigned ms_backup_input_val = 0;
static unsigned ms_backup_output_val = 0;
static unsigned MS_BAKUP_INPUT_REG = (unsigned)&ms_backup_input_val;
static unsigned MS_BAKUP_OUTPUT_REG = (unsigned)&ms_backup_output_val;

unsigned MS_BS_OUTPUT_EN_REG;
unsigned MS_BS_OUTPUT_EN_MASK;
unsigned MS_BS_OUTPUT_REG;
unsigned MS_BS_OUTPUT_MASK;
unsigned MS_CLK_OUTPUT_EN_REG;
unsigned MS_CLK_OUTPUT_EN_MASK;
unsigned MS_CLK_OUTPUT_REG;
unsigned MS_CLK_OUTPUT_MASK;
unsigned MS_DAT_OUTPUT_EN_REG;
unsigned MS_DAT0_OUTPUT_EN_MASK;
unsigned MS_DAT0_3_OUTPUT_EN_MASK;
unsigned MS_DAT_INPUT_REG;
unsigned MS_DAT_OUTPUT_REG;
unsigned MS_DAT0_INPUT_MASK;
unsigned MS_DAT0_OUTPUT_MASK;
unsigned MS_DAT0_3_INPUT_MASK;
unsigned MS_DAT0_3_OUTPUT_MASK;
unsigned MS_DAT_INPUT_OFFSET;
unsigned MS_DAT_OUTPUT_OFFSET;
unsigned MS_INS_OUTPUT_EN_REG;
unsigned MS_INS_OUTPUT_EN_MASK;
unsigned MS_INS_INPUT_REG;
unsigned MS_INS_INPUT_MASK;
unsigned MS_PWR_OUTPUT_EN_REG;
unsigned MS_PWR_OUTPUT_EN_MASK;
unsigned MS_PWR_OUTPUT_REG;
unsigned MS_PWR_OUTPUT_MASK;
unsigned MS_PWR_EN_LEVEL;
unsigned MS_WORK_MODE;

void ms_insert_detector(struct memory_card *card)
{
	MS_MSPRO_Card_Info_t *ms_mspro_info = (MS_MSPRO_Card_Info_t *)card->card_info;
	
	int ret = ms_mspro_check_insert(ms_mspro_info);
	
	if(ret)
		card->card_status = CARD_INSERTED;
	else
		card->card_status = CARD_REMOVED;

	return;
}

extern unsigned char disable_port_switch;
void ms_open(struct memory_card *card)
{
	int ret;
	MS_MSPRO_Card_Info_t *ms_mspro_info = (MS_MSPRO_Card_Info_t *)card->card_info;
	
	ret = ms_mspro_init(ms_mspro_info);
	if(ret)
	{
		disable_port_switch = 1;
		ret = ms_mspro_init(ms_mspro_info);
	}
	disable_port_switch = 0;
	
	card->capacity = ms_mspro_info->blk_nums;
	if(ret)
		card->unit_state = CARD_UNIT_READY;
	else
		card->unit_state = CARD_UNIT_PROCESSED;

	return;
}

void ms_close(struct memory_card *card)
{
	MS_MSPRO_Card_Info_t *ms_mspro_info = (MS_MSPRO_Card_Info_t *)card->card_info;
	
	if (ms_mspro_info->data_buf != NULL)
	{
		dma_free_coherent(NULL, 4096, ms_mspro_info->data_buf, (dma_addr_t )ms_mspro_info->data_phy_buf);
		ms_mspro_info->data_buf  = NULL;
		ms_mspro_info->data_phy_buf = NULL;
	}
	
	if (ms_mspro_info->ms_mspro_buf != NULL)
	{
		dma_free_coherent(NULL, 4096, ms_mspro_info->ms_mspro_buf, (dma_addr_t )ms_mspro_info->ms_mspro_phy_buf);
		ms_mspro_info->ms_mspro_buf  = NULL;
		ms_mspro_info->ms_mspro_phy_buf = NULL;
	}
	
	ms_mspro_exit(ms_mspro_info);
	ms_mspro_free(ms_mspro_info);
	ms_mspro_info = NULL;
	card->card_info = NULL;
	card->unit_state =  CARD_UNIT_PROCESSED;
	
	return;
}

unsigned char ms_read_info(struct memory_card *card, u32 *blk_length, u32 *capacity, u32 *raw_cid)
{
	MS_MSPRO_Card_Info_t *ms_mspro_info = (MS_MSPRO_Card_Info_t *)card->card_info;
	
	if(ms_mspro_info->inited_flag)
	{
		if(blk_length)
			*blk_length = 512;
		if(capacity)
			*capacity = ms_mspro_info->blk_nums;
		if(raw_cid)
			memcpy(raw_cid, &(ms_mspro_info->raw_cid), sizeof(ms_mspro_info->raw_cid));
		return 0;
	}
	else
		return 1;
}

static void ms_io_init(struct memory_card *card)
{
	struct aml_card_info *aml_card_info = card->card_plat_info;
	MS_WORK_MODE = aml_card_info->work_mode;

	switch (aml_card_info->io_pad_type) {

		case SDIO_GPIOA_0_5:
			MS_BS_OUTPUT_EN_REG = EGPIO_GPIOA_ENABLE;
			MS_BS_OUTPUT_EN_MASK = PREG_IO_9_MASK;
			MS_BS_OUTPUT_REG = EGPIO_GPIOA_OUTPUT;
			MS_BS_OUTPUT_MASK = PREG_IO_9_MASK;

			MS_CLK_OUTPUT_EN_REG = EGPIO_GPIOA_ENABLE;
			MS_CLK_OUTPUT_EN_MASK = PREG_IO_8_MASK;
			MS_CLK_OUTPUT_REG = EGPIO_GPIOA_OUTPUT;
			MS_CLK_OUTPUT_MASK = PREG_IO_8_MASK;

			MS_DAT_OUTPUT_EN_REG = EGPIO_GPIOA_ENABLE;
			MS_DAT0_OUTPUT_EN_MASK = PREG_IO_4_MASK;
			MS_DAT0_3_OUTPUT_EN_MASK = PREG_IO_4_7_MASK;
			MS_DAT_INPUT_REG = EGPIO_GPIOA_INPUT;
			MS_DAT_OUTPUT_REG = EGPIO_GPIOA_OUTPUT;
			MS_DAT0_INPUT_MASK = PREG_IO_4_MASK;
			MS_DAT0_OUTPUT_MASK = PREG_IO_4_MASK;
			MS_DAT0_3_INPUT_MASK = PREG_IO_4_7_MASK;
			MS_DAT0_3_OUTPUT_MASK = PREG_IO_4_7_MASK;
			MS_DAT_INPUT_OFFSET = 4;
			MS_DAT_OUTPUT_OFFSET = 4;
			break;

		case SDIO_GPIOA_9_14:
			MS_BS_OUTPUT_EN_REG = EGPIO_GPIOA_ENABLE;
			MS_BS_OUTPUT_EN_MASK = PREG_IO_18_MASK;
			MS_BS_OUTPUT_REG = EGPIO_GPIOA_OUTPUT;
			MS_BS_OUTPUT_MASK = PREG_IO_18_MASK;

			MS_CLK_OUTPUT_EN_REG = EGPIO_GPIOA_ENABLE;
			MS_CLK_OUTPUT_EN_MASK = PREG_IO_17_MASK;
			MS_CLK_OUTPUT_REG = EGPIO_GPIOA_OUTPUT;
			MS_CLK_OUTPUT_MASK = PREG_IO_17_MASK;

			MS_DAT_OUTPUT_EN_REG = EGPIO_GPIOA_ENABLE;
			MS_DAT0_OUTPUT_EN_MASK = PREG_IO_13_MASK;
			MS_DAT0_3_OUTPUT_EN_MASK = PREG_IO_13_16_MASK;
			MS_DAT_INPUT_REG = EGPIO_GPIOA_INPUT;
			MS_DAT_OUTPUT_REG = EGPIO_GPIOA_OUTPUT;
			MS_DAT0_INPUT_MASK = PREG_IO_13_MASK;
			MS_DAT0_OUTPUT_MASK = PREG_IO_13_MASK;
			MS_DAT0_3_INPUT_MASK = PREG_IO_13_16_MASK;
			MS_DAT0_3_OUTPUT_MASK = PREG_IO_13_16_MASK;
			MS_DAT_INPUT_OFFSET = 13;
			MS_DAT_OUTPUT_OFFSET = 13;
			break;

		case SDIO_GPIOB_2_7:
			MS_BS_OUTPUT_EN_REG = EGPIO_GPIOA_ENABLE;
			MS_BS_OUTPUT_EN_MASK = PREG_IO_21_MASK;
			MS_BS_OUTPUT_REG = EGPIO_GPIOA_OUTPUT;
			MS_BS_OUTPUT_MASK = PREG_IO_21_MASK;

			MS_CLK_OUTPUT_EN_REG = EGPIO_GPIOA_ENABLE;
			MS_CLK_OUTPUT_EN_MASK = PREG_IO_22_MASK;
			MS_CLK_OUTPUT_REG = EGPIO_GPIOA_OUTPUT;
			MS_CLK_OUTPUT_MASK = PREG_IO_22_MASK;

			MS_DAT_OUTPUT_EN_REG = EGPIO_GPIOA_ENABLE;
			MS_DAT0_OUTPUT_EN_MASK = PREG_IO_23_MASK;
			MS_DAT0_3_OUTPUT_EN_MASK = PREG_IO_23_26_MASK;
			MS_DAT_INPUT_REG = EGPIO_GPIOA_INPUT;
			MS_DAT_OUTPUT_REG = EGPIO_GPIOA_OUTPUT;
			MS_DAT0_INPUT_MASK = PREG_IO_23_MASK;
			MS_DAT0_OUTPUT_MASK = PREG_IO_23_MASK;
			MS_DAT0_3_INPUT_MASK = PREG_IO_23_26_MASK;
			MS_DAT0_3_OUTPUT_MASK = PREG_IO_23_26_MASK;
			MS_DAT_INPUT_OFFSET = 23;
			MS_DAT_OUTPUT_OFFSET = 23;
			break;

		case SDIO_GPIOE_6_11:
			MS_BS_OUTPUT_EN_REG = EGPIO_GPIOE_ENABLE;
			MS_BS_OUTPUT_EN_MASK = PREG_IO_7_MASK;
			MS_BS_OUTPUT_REG = EGPIO_GPIOE_OUTPUT;
			MS_BS_OUTPUT_MASK = PREG_IO_7_MASK;

			MS_CLK_OUTPUT_EN_REG = EGPIO_GPIOE_ENABLE;
			MS_CLK_OUTPUT_EN_MASK = PREG_IO_6_MASK;
			MS_CLK_OUTPUT_REG = EGPIO_GPIOE_OUTPUT;
			MS_CLK_OUTPUT_MASK = PREG_IO_6_MASK;

			MS_DAT_OUTPUT_EN_REG = EGPIO_GPIOE_ENABLE;
			MS_DAT0_OUTPUT_EN_MASK = PREG_IO_8_MASK;
			MS_DAT0_3_OUTPUT_EN_MASK = PREG_IO_8_11_MASK;
			MS_DAT_INPUT_REG = EGPIO_GPIOE_INPUT;
			MS_DAT_OUTPUT_REG = EGPIO_GPIOE_OUTPUT;
			MS_DAT0_INPUT_MASK = PREG_IO_8_MASK;
			MS_DAT0_OUTPUT_MASK = PREG_IO_8_MASK;
			MS_DAT0_3_INPUT_MASK = PREG_IO_8_11_MASK;
			MS_DAT0_3_OUTPUT_MASK = PREG_IO_8_11_MASK;
			MS_DAT_INPUT_OFFSET = 8;
			MS_DAT_OUTPUT_OFFSET = 8;
			break;

        default:
			printk("Warning couldn`t find any valid hw io pad!!!\n");
            break;
	}

	if (aml_card_info->card_ins_en_reg) {
		MS_INS_OUTPUT_EN_REG = aml_card_info->card_ins_en_reg;
		MS_INS_OUTPUT_EN_MASK = aml_card_info->card_ins_en_mask;
		MS_INS_INPUT_REG = aml_card_info->card_ins_input_reg;
		MS_INS_INPUT_MASK = aml_card_info->card_ins_input_mask;
	}
	else {
		MS_INS_OUTPUT_EN_REG = MS_BAKUP_OUTPUT_REG;
		MS_INS_OUTPUT_EN_MASK = 1;
		MS_INS_INPUT_REG = MS_BAKUP_INPUT_REG;
		MS_INS_INPUT_MASK = 1;
	}

	if (aml_card_info->card_power_en_reg) {
		MS_PWR_OUTPUT_EN_REG = aml_card_info->card_power_en_reg;
		MS_PWR_OUTPUT_EN_MASK = aml_card_info->card_power_en_mask;
		MS_PWR_OUTPUT_REG = aml_card_info->card_power_output_reg;
		MS_PWR_OUTPUT_MASK = aml_card_info->card_power_output_mask;
		MS_PWR_EN_LEVEL = aml_card_info->card_power_en_lev;
	}
	else {
		MS_PWR_OUTPUT_EN_REG = MS_BAKUP_OUTPUT_REG;
		MS_PWR_OUTPUT_EN_MASK = 1;
		MS_PWR_OUTPUT_REG = MS_BAKUP_OUTPUT_REG;
		MS_PWR_OUTPUT_MASK = 1;
		MS_PWR_EN_LEVEL = 0;	
	}
	return;
}

static int ms_request(struct memory_card *card, struct card_blk_request *brq)
{
	MS_MSPRO_Card_Info_t *ms_info = (MS_MSPRO_Card_Info_t *)card->card_info;
	unsigned int lba, byte_cnt;
	unsigned char *data_buf;

	lba = brq->card_data.lba;
	byte_cnt = brq->card_data.blk_size * brq->card_data.blk_nums;
	data_buf = brq->crq.buf;

	if(brq->crq.cmd == READ) {
		//printk("R(%d,%d)\n", lba, byte_cnt);
		brq->card_data.error = ms_mspro_read_data(ms_info, lba, byte_cnt, data_buf);
	}
	else if(brq->crq.cmd == WRITE) {
		//printk("W(%d,%d)\n", lba, byte_cnt);
		brq->card_data.error = ms_mspro_write_data(ms_info, lba, byte_cnt, data_buf);
	}

	return 0;
}

int ms_probe(struct memory_card *card)
{
	struct aml_card_info *aml_card_info = card->card_plat_info;

	MS_MSPRO_Card_Info_t *ms_mspro_info = ms_mspro_malloc(sizeof(MS_MSPRO_Card_Info_t), GFP_KERNEL);
	if (ms_mspro_info == NULL)
		return -ENOMEM;

	if (card->host->dma_buf != NULL) {
		ms_mspro_info->dma_buf = card->host->dma_buf;
		ms_mspro_info->dma_phy_buf = card->host->dma_phy_buf;
	}

	card->card_info = ms_mspro_info;
	card->card_io_init = ms_io_init;
	card->card_detector = ms_insert_detector;
	card->card_insert_process = ms_open;
	card->card_remove_process = ms_close;
	card->card_request_process = ms_request;

	if (aml_card_info->card_extern_init)
		aml_card_info->card_extern_init();
	card->card_io_init(card);
	ms_mspro_info->io_pad_type = aml_card_info->io_pad_type;
	ms_mspro_prepare_init(ms_mspro_info);

	ms_mspro_info->data_buf = dma_alloc_coherent(NULL, PAGE_CACHE_SIZE, (dma_addr_t *)&ms_mspro_info->data_phy_buf, GFP_KERNEL);
	if(ms_mspro_info->data_buf == NULL)
		return -ENOMEM;
	
	ms_mspro_info->ms_mspro_buf = dma_alloc_coherent(NULL, PAGE_CACHE_SIZE, (dma_addr_t *)&ms_mspro_info->ms_mspro_phy_buf, GFP_KERNEL);
	if(ms_mspro_info->ms_mspro_buf == NULL)
		return -ENOMEM;

	return 0;
}
