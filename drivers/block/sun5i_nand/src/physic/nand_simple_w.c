/*********************************************************************************************************
*                                                                NAND FLASH DRIVER
*								(c) Copyright 2008, SoftWinners Co,Ld.
*                                          			    All Right Reserved
*file : nand_simple.c
*description : this file creates some physic basic access function based on single plane for boot .
*history :
*	v0.1  2008-03-26 Richard
* v0.2  2009-9-3 penggang modified for 1615
*			
*********************************************************************************************************/
#include "../include/nand_type.h"
#include "../include/nand_physic.h"
#include "../include/nand_simple.h"
#include "../../nfc/nfc.h"


extern __s32 _read_status(__u32 cmd_value, __u32 nBank);
extern void _add_cmd_list(NFC_CMD_LIST *cmd,__u32 value,__u32 addr_cycle,__u8 *addr,__u8 data_fetch_flag,
					__u8 main_data_fetch,__u32 bytecnt,__u8 wait_rb_flag);
extern __u8 _cal_real_chip(__u32 global_bank);
extern __u8 _cal_real_rb(__u32 chip);
extern void _cal_addr_in_chip(__u32 block, __u32 page, __u32 sector,__u8 *addr, __u8 cycle);
extern void _pending_dma_irq_sem(void);
extern void _pending_rb_irq_sem(void);
extern __u32 _cal_random_seed(__u32 page);

/***************************************************************************
*************************write one align single page data**************************
****************************************************************************/

__s32 _write_signle_page (struct boot_physical_param *writeop,__u32 program1,__u32 program2,__u8 dma_wait_mode, __u8 rb_wait_mode )
{
	__s32 ret;
	__u32 rb;
	__u32 random_seed;
	//__u8 *sparebuf;
	__u8 sparebuf[4*16];
	__u8 addr[5];
	NFC_CMD_LIST cmd_list[4];
	__u32 list_len,i,addr_cycle;

	MEMSET(sparebuf, 0xff, SECTOR_CNT_OF_SINGLE_PAGE * 4);
	if (writeop->oobbuf){
		MEMCPY(sparebuf,writeop->oobbuf,SECTOR_CNT_OF_SINGLE_PAGE * 4);
	}
	/*create cmd list*/
	addr_cycle = (SECTOR_CNT_OF_SINGLE_PAGE == 1)?4:5;

	/*the cammand have no corresponding feature if IGNORE was set, */
	_cal_addr_in_chip(writeop->block,writeop->page,0,addr,addr_cycle);
	_add_cmd_list(cmd_list,program1,addr_cycle,addr,NFC_DATA_FETCH,NFC_IGNORE,NFC_IGNORE,NFC_NO_WAIT_RB);
	_add_cmd_list(cmd_list + 1,0x85,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE);
	_add_cmd_list(cmd_list + 2,program2,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE);

	list_len = 3;
	for(i = 0; i < list_len - 1; i++){
		cmd_list[i].next = &(cmd_list[i+1]);
	}
	rb = _cal_real_rb(writeop->chip);
	NFC_SelectChip(writeop->chip);
	NFC_SelectRb(rb);

	if(SUPPORT_RANDOM)
    {
        random_seed = _cal_random_seed(writeop->page);
		NFC_SetRandomSeed(random_seed);
		NFC_RandomEnable();
		ret = NFC_Write(cmd_list, writeop->mainbuf, sparebuf, dma_wait_mode, rb_wait_mode, NFC_PAGE_MODE);
		NFC_RandomDisable();
    }
    else
    {
        ret = NFC_Write(cmd_list, writeop->mainbuf, sparebuf, dma_wait_mode, rb_wait_mode, NFC_PAGE_MODE);
    }


	NFC_DeSelectChip(writeop->chip);
	NFC_DeSelectRb(rb);
	if (dma_wait_mode)
		_pending_dma_irq_sem();

	return ret;

}

__s32 _write_signle_page_seq (struct boot_physical_param *writeop,__u32 program1,__u32 program2,__u8 dma_wait_mode, __u8 rb_wait_mode )
{
	__s32 ret;
	__u32 rb;
	__u32 random_seed;
	//__u8 *sparebuf;
	__u8 sparebuf[4*16];
	__u8 addr[5];
	NFC_CMD_LIST cmd_list[4];
	__u32 list_len,i,addr_cycle;

	MEMSET(sparebuf, 0xff, SECTOR_CNT_OF_SINGLE_PAGE * 4);
	if (writeop->oobbuf){
		MEMCPY(sparebuf,writeop->oobbuf,SECTOR_CNT_OF_SINGLE_PAGE * 4);
	}
	/*create cmd list*/
	addr_cycle = (SECTOR_CNT_OF_SINGLE_PAGE == 1)?4:5;

	/*the cammand have no corresponding feature if IGNORE was set, */
	_cal_addr_in_chip(writeop->block,writeop->page,0,addr,addr_cycle);
	_add_cmd_list(cmd_list,program1,addr_cycle,addr,NFC_DATA_FETCH,NFC_IGNORE,NFC_IGNORE,NFC_NO_WAIT_RB);
	_add_cmd_list(cmd_list + 1,0x85,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE);
	_add_cmd_list(cmd_list + 2,program2,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE);

	list_len = 3;
	for(i = 0; i < list_len - 1; i++){
		cmd_list[i].next = &(cmd_list[i+1]);
	}
	rb = _cal_real_rb(writeop->chip);
	NFC_SelectChip(writeop->chip);
	NFC_SelectRb(rb);


	if(SUPPORT_RANDOM)
	{
	    random_seed = 0x4a80;
		NFC_SetRandomSeed(random_seed);
		NFC_RandomEnable();
		ret = NFC_Write_Seq(cmd_list, writeop->mainbuf, sparebuf, dma_wait_mode, rb_wait_mode, NFC_PAGE_MODE);
		NFC_RandomDisable();
	}
	else
	{
	    ret = NFC_Write_Seq(cmd_list, writeop->mainbuf, sparebuf, dma_wait_mode, rb_wait_mode, NFC_PAGE_MODE);
	}


	NFC_DeSelectChip(writeop->chip);
	NFC_DeSelectRb(rb);
	if (dma_wait_mode)
		_pending_dma_irq_sem();

	return ret;

}

__s32 _write_signle_page_1K (struct boot_physical_param *writeop,__u32 program1,__u32 program2,__u8 dma_wait_mode, __u8 rb_wait_mode )
{
	__s32 ret;
	__u32 rb;
	__u32 random_seed;
	//__u8 *sparebuf;
	__u8 sparebuf[4*16];
	__u8 addr[5];
	NFC_CMD_LIST cmd_list[4];
	__u32 list_len,i,addr_cycle;

	MEMSET(sparebuf, 0xff, SECTOR_CNT_OF_SINGLE_PAGE * 4);
	if (writeop->oobbuf){
		MEMCPY(sparebuf,writeop->oobbuf,SECTOR_CNT_OF_SINGLE_PAGE * 4);
	}
	/*create cmd list*/
	addr_cycle = (SECTOR_CNT_OF_SINGLE_PAGE == 1)?4:5;

	/*the cammand have no corresponding feature if IGNORE was set, */
	_cal_addr_in_chip(writeop->block,writeop->page,0,addr,addr_cycle);
	_add_cmd_list(cmd_list,program1,addr_cycle,addr,NFC_DATA_FETCH,NFC_IGNORE,NFC_IGNORE,NFC_NO_WAIT_RB);
	_add_cmd_list(cmd_list + 1,0x85,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE);
	_add_cmd_list(cmd_list + 2,program2,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE);

	list_len = 3;
	for(i = 0; i < list_len - 1; i++){
		cmd_list[i].next = &(cmd_list[i+1]);
	}
	rb = _cal_real_rb(writeop->chip);
	NFC_SelectChip(writeop->chip);
	NFC_SelectRb(rb);


	if(1)
	{
	    random_seed = 0x4a80;
		NFC_SetRandomSeed(random_seed);
		NFC_RandomEnable();
		ret = NFC_Write_1K(cmd_list, writeop->mainbuf, sparebuf, dma_wait_mode, rb_wait_mode, NFC_PAGE_MODE);
		NFC_RandomDisable();
	}
	else
	{
	    ret = NFC_Write_1K(cmd_list, writeop->mainbuf, sparebuf, dma_wait_mode, rb_wait_mode, NFC_PAGE_MODE);
	}


	NFC_DeSelectChip(writeop->chip);
	NFC_DeSelectRb(rb);
	if (dma_wait_mode)
		_pending_dma_irq_sem();

	return ret;

}

__s32 _erase_single_block(struct boot_physical_param *eraseop)
{
	__s32 ret;
	__u32 rb;
	__u8 addr[5];
	NFC_CMD_LIST cmd_list[4];
	__u32 list_len,i;

	/*create cmd list*/
	/*the cammand have no corresponding feature if IGNORE was set, */
	list_len = 2;
	_cal_addr_in_chip(eraseop->block,0,0,addr,3);
	_add_cmd_list(cmd_list,0x60,3,addr,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE);
	_add_cmd_list(cmd_list + 1,0xd0,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE);

	for(i = 0; i < list_len - 1; i++){
		cmd_list[i].next = &(cmd_list[i+1]);
	}

	rb = _cal_real_rb(eraseop->chip);
	NFC_SelectChip(eraseop->chip);
	NFC_SelectRb(rb);
	ret = NFC_Erase(cmd_list, 0);
	NFC_DeSelectChip(eraseop->chip);
	NFC_DeSelectRb(rb);
	return ret;
}
__s32 PHY_SimpleWrite (struct boot_physical_param *writeop)
{
	__s32 status;
	__u32 rb;

	__s32 ret;

	ret = _write_signle_page(writeop,0x80,0x10,0,0);
	if (ret)
		return FAIL;
	rb = _cal_real_rb(writeop->chip);
	NFC_SelectChip(writeop->chip);
	NFC_SelectRb(rb);
	/*get status*/
	while(1){
		status = _read_status(0x70,writeop->chip);
		if (status < 0)
			return status;

		if (status & NAND_STATUS_READY)
			break;
	}
	if (status & NAND_OPERATE_FAIL)
		ret = BADBLOCK;
	NFC_DeSelectChip(writeop->chip);
	NFC_DeSelectRb(rb);

	return ret;
}

__s32 PHY_SimpleWrite_Seq (struct boot_physical_param *writeop)
{
	__s32 status;
	__u32 rb;

	__s32 ret;

	ret = _write_signle_page_seq(writeop,0x80,0x10,0,0);
	if (ret)
		return FAIL;
	rb = _cal_real_rb(writeop->chip);
	NFC_SelectChip(writeop->chip);
	NFC_SelectRb(rb);
	/*get status*/
	while(1){
		status = _read_status(0x70,writeop->chip);
		if (status < 0)
			return status;

		if (status & NAND_STATUS_READY)
			break;
	}
	if (status & NAND_OPERATE_FAIL)
		ret = BADBLOCK;
	NFC_DeSelectChip(writeop->chip);
	NFC_DeSelectRb(rb);

	return ret;
}

__s32 PHY_SimpleWrite_1K (struct boot_physical_param *writeop)
{
	__s32 status;
	__u32 rb;

	__s32 ret;

	ret = _write_signle_page_1K(writeop,0x80,0x10,0,0);
	if (ret)
		return FAIL;
	rb = _cal_real_rb(writeop->chip);
	NFC_SelectChip(writeop->chip);
	NFC_SelectRb(rb);
	/*get status*/
	while(1){
		status = _read_status(0x70,writeop->chip);
		if (status < 0)
			return status;

		if (status & NAND_STATUS_READY)
			break;
	}
	if (status & NAND_OPERATE_FAIL)
		ret = BADBLOCK;
	NFC_DeSelectChip(writeop->chip);
	NFC_DeSelectRb(rb);

	return ret;
}
//#pragma arm section code="PHY_SimpleErase"
__s32 PHY_SimpleErase (struct boot_physical_param *eraseop )
{
	__s32 status;
	__s32 ret;
	__u32 rb;


	ret = _erase_single_block(eraseop);
	if (ret)
		return FAIL;
	rb = _cal_real_rb(eraseop->chip);
	NFC_SelectChip(eraseop->chip);
	NFC_SelectRb(rb);
	/*get status*/
	while(1){
		status = _read_status(0x70,eraseop->chip);
		if (status & NAND_STATUS_READY)
			break;
	}
	if (status & NAND_OPERATE_FAIL)
		ret = BADBLOCK;

	NFC_DeSelectChip(eraseop->chip);
	NFC_DeSelectRb(rb);
	return ret;

}

