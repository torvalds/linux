/*********************************************************************************************************
*                                                                NAND FLASH DRIVER
*								(c) Copyright 2008, SoftWinners Co,Ld.
*                                          			    All Right Reserved
*file : nand_phy.c
*description : this file creates some physic optimize access function for system .
*history :
*	v0.1  2008-03-26 Richard
* v0.2  2009-9-3 penggang modified for 1615
*			
*********************************************************************************************************/
#include "../include/nand_type.h"
#include "../include/nand_physic.h"
#include "../include/nand_simple.h"
#include "../../nfc/nfc.h"

extern  __u32 RetryCount[8];
extern void _add_cmd_list(NFC_CMD_LIST *cmd,__u32 value,__u32 addr_cycle,__u8 *addr,__u8 data_fetch_flag,
	__u8 main_data_fetch,__u32 bytecnt,__u8 wait_rb_flag);
extern void _cal_addr_in_chip(__u32 block, __u32 page, __u32 sector,__u8 *addr, __u8 cycle);
extern __s32 _read_status(__u32 cmd_value, __u32 nBank);
extern __s32 _write_signle_page (struct boot_physical_param *writeop,__u32 program1,__u32 program2,__u8 dma_wait_mode, __u8 rb_wait_mode); 
extern __s32 _read_single_page(struct boot_physical_param * readop,__u8 dma_wait_mode);
extern void _pending_dma_irq_sem(void);
extern __s32 _wait_rb_ready(__u32 chip);
extern __s32 _wait_rb_ready_int(__u32 chip);
extern __u8 _cal_real_chip(__u32 global_bank);
extern __u8 _cal_real_rb(__u32 chip);
extern __u32 _cal_random_seed(__u32 page);


/**********************************************************************
***********************translate block in bank into baock in chip**************
***********************************************************************/
__u32 _cal_block_in_chip(__u32 global_bank, __u32 super_blk_within_bank)
{
	__u32 blk_within_chip;
	__u32 single_blk_within_bank;
	__u32 bank_base;	

	/*translate block 0 within  bank into blcok number within chip*/
	bank_base = global_bank%BNK_CNT_OF_CHIP*BLOCK_CNT_OF_DIE;
	
	if (SUPPORT_MULTI_PROGRAM ){
		 single_blk_within_bank = super_blk_within_bank * PLANE_CNT_OF_DIE - 
		 	super_blk_within_bank%MULTI_PLANE_BLOCK_OFFSET;
	}
	
	else{
		single_blk_within_bank = super_blk_within_bank;
	}

	blk_within_chip = bank_base + single_blk_within_bank;

	if (blk_within_chip >= DIE_CNT_OF_CHIP * BLOCK_CNT_OF_DIE)
		blk_within_chip = 0xffffffff;
	
	return blk_within_chip;
}
/*
************************************************************************************************************************
*                           PHYSICAL BLOCK ERASE
*
*Description: Erase one nand flash physical block.
*
*Arguments  : pBlkAdr   the parameter of the physical block which need be erased.
*
*Return     : the result of the block erase;
*               = 0     erase physical block successful;
*               = -1    erase physical block failed.
************************************************************************************************************************
*/
__s32 _read_single_page_seq(struct boot_physical_param *readop,__u8 dma_wait_mode)
{
	__s32 ret;
	__u32 rb;
	__u32 random_seed;

	//__u8 *sparebuf;
	__u8 sparebuf[4*16];
	__u8 addr[5];
	NFC_CMD_LIST cmd_list[4];
	__u32 list_len,i;
	
	//sparebuf = (__u8 *)MALLOC(SECTOR_CNT_OF_SINGLE_PAGE * 4);	
	/*create cmd list*/
	/*samll block*/
	if (SECTOR_CNT_OF_SINGLE_PAGE == 1){		
		_cal_addr_in_chip(readop->block,readop->page,0,addr,4);
		_add_cmd_list(cmd_list,0x00,4,addr,NFC_DATA_FETCH,NFC_IGNORE,NFC_IGNORE,NFC_WAIT_RB);
	}
	/*large block*/
	else{
		/*the cammand have no corresponding feature if IGNORE was set, */		
		_cal_addr_in_chip(readop->block,readop->page,0,addr,5);
		_add_cmd_list(cmd_list,0x00,5,addr,NFC_NO_DATA_FETCH,NFC_IGNORE,NFC_IGNORE,NFC_NO_WAIT_RB);
		
	}	
	_add_cmd_list(cmd_list + 1,0x05,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE);
	_add_cmd_list(cmd_list + 2,0xe0,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE);
	_add_cmd_list(cmd_list + 3,0x30,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE);
	list_len = 4;
	for(i = 0; i < list_len - 1; i++){
		cmd_list[i].next = &(cmd_list[i+1]);
	}

	/*wait rb ready*/
	ret = _wait_rb_ready(readop->chip);
	if (ret)
		return ret;
	
	rb = _cal_real_rb(readop->chip);
	NFC_SelectChip(readop->chip);
	NFC_SelectRb(rb);

	if(SUPPORT_RANDOM)
	{
		//random_seed = _cal_random_seed(readop->page);
		random_seed = 0x4a80;
		NFC_SetRandomSeed(random_seed);
		NFC_RandomEnable();
		ret = NFC_Read_Seq(cmd_list, readop->mainbuf, sparebuf, dma_wait_mode , NFC_PAGE_MODE);
		NFC_RandomDisable();
		if(ret)
			ret = NFC_Read_Seq(cmd_list, readop->mainbuf, sparebuf, dma_wait_mode , NFC_PAGE_MODE);
	}
	else
	{
		ret = NFC_Read_Seq(cmd_list, readop->mainbuf, sparebuf, dma_wait_mode , NFC_PAGE_MODE);
	}
		

	
	
	if (dma_wait_mode)
		_pending_dma_irq_sem();
	
	if (readop->oobbuf){
		MEMCPY(readop->oobbuf,sparebuf, 2 * 4);
	}

	NFC_DeSelectChip(readop->chip);
	NFC_DeSelectRb(rb);
	
	//FREE(sparebuf);
	return ret;
}

__s32 _read_single_page_1K(struct boot_physical_param *readop,__u8 dma_wait_mode)
{
	__s32 ret;
	__u32 rb;
	__u32 random_seed;
	__u8 sparebuf[4*16];
	__u8 addr[5];
	NFC_CMD_LIST cmd_list[4];
	__u32 list_len,i;
	
	//sparebuf = (__u8 *)MALLOC(SECTOR_CNT_OF_SINGLE_PAGE * 4);	
	/*create cmd list*/
	/*samll block*/
	if (SECTOR_CNT_OF_SINGLE_PAGE == 1){		
		_cal_addr_in_chip(readop->block,readop->page,0,addr,4);
		_add_cmd_list(cmd_list,0x00,4,addr,NFC_DATA_FETCH,NFC_IGNORE,NFC_IGNORE,NFC_WAIT_RB);
	}
	/*large block*/
	else{
		/*the cammand have no corresponding feature if IGNORE was set, */		
		_cal_addr_in_chip(readop->block,readop->page,0,addr,5);
		_add_cmd_list(cmd_list,0x00,5,addr,NFC_NO_DATA_FETCH,NFC_IGNORE,NFC_IGNORE,NFC_NO_WAIT_RB);
		
	}	
	_add_cmd_list(cmd_list + 1,0x05,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE);
	_add_cmd_list(cmd_list + 2,0xe0,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE);
	_add_cmd_list(cmd_list + 3,0x30,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE);
	list_len = 4;
	for(i = 0; i < list_len - 1; i++){
		cmd_list[i].next = &(cmd_list[i+1]);
	}

	/*wait rb ready*/
	ret = _wait_rb_ready(readop->chip);
	if (ret)
		return ret;
	
	rb = _cal_real_rb(readop->chip);
	NFC_SelectChip(readop->chip);
	NFC_SelectRb(rb);


	if(1)
	{
		//random_seed = _cal_random_seed(readop->page);
		random_seed = 0x4a80;
		NFC_SetRandomSeed(random_seed);
		NFC_RandomEnable();
		ret = NFC_Read_1K(cmd_list, readop->mainbuf, sparebuf, dma_wait_mode , NFC_PAGE_MODE);
		NFC_RandomDisable();
		if(ret)
			ret = NFC_Read_1K(cmd_list, readop->mainbuf, sparebuf, dma_wait_mode , NFC_PAGE_MODE);
		
	}
	else
	{
		ret = NFC_Read_1K(cmd_list, readop->mainbuf, sparebuf, dma_wait_mode , NFC_PAGE_MODE);
	}

	
	
	if (dma_wait_mode)
		_pending_dma_irq_sem();
	
	if (readop->oobbuf){
		MEMCPY(readop->oobbuf,sparebuf, 2 * 4);
	}

	NFC_DeSelectChip(readop->chip);
	NFC_DeSelectRb(rb);
	
	//FREE(sparebuf);
	return ret;
}

__s32 _read_single_page_spare(struct boot_physical_param *readop,__u8 dma_wait_mode)
{
	__s32 ret;
	__u32 k = 0;
	__u32 rb;
	__u32 random_seed;
	__u8 sparebuf[4*16];
	__u8 default_value[16];
	__u8 addr[5];
	NFC_CMD_LIST cmd_list[4];
	__u32 list_len,i;

	//sparebuf = (__u8 *)MALLOC(SECTOR_CNT_OF_SINGLE_PAGE * 4);
	/*create cmd list*/
	/*samll block*/
	if (SECTOR_CNT_OF_SINGLE_PAGE == 1){
		_cal_addr_in_chip(readop->block,readop->page,0,addr,4);
		_add_cmd_list(cmd_list,0x00,4,addr,NFC_DATA_FETCH,NFC_IGNORE,NFC_IGNORE,NFC_WAIT_RB);
	}
	/*large block*/
	else{
		/*the cammand have no corresponding feature if IGNORE was set, */
		_cal_addr_in_chip(readop->block,readop->page,0,addr,5);
		_add_cmd_list(cmd_list,0x00,5,addr,NFC_NO_DATA_FETCH,NFC_IGNORE,NFC_IGNORE,NFC_NO_WAIT_RB);

	}
	_add_cmd_list(cmd_list + 1,0x05,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE);
	_add_cmd_list(cmd_list + 2,0xe0,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE);
	_add_cmd_list(cmd_list + 3,0x30,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE);
	list_len = 4;
	for(i = 0; i < list_len - 1; i++){
		cmd_list[i].next = &(cmd_list[i+1]);
	}

	/*wait rb ready*/
	ret = _wait_rb_ready(readop->chip);
	if (ret)
		return ret;

	rb = _cal_real_rb(readop->chip);
	NFC_SelectChip(readop->chip);
	NFC_SelectRb(rb);

    if(SUPPORT_READ_RETRY)  
    {
        if((READ_RETRY_MODE>=0x10)&&(READ_RETRY_MODE<0x30))  //toshiba & samsung mode
            RetryCount[readop->chip] = 0;
            
        for( k = 0; k<READ_RETRY_CYCLE+1;k++)
		{
			if(RetryCount[readop->chip]==(READ_RETRY_CYCLE+1))
				RetryCount[readop->chip] = 0;

			if(k>0)
			{
			    if(NFC_ReadRetry(readop->chip,RetryCount[readop->chip],READ_RETRY_TYPE))
			    {
			        PHY_ERR("[Read_single_page_spare] NFC_ReadRetry fail \n");
			        return -1;
			    }
			}

			if(SUPPORT_RANDOM)
			{
				random_seed = _cal_random_seed(readop->page);
				NFC_SetRandomSeed(random_seed);
				NFC_RandomEnable();
				ret = NFC_Read_Spare(cmd_list, readop->mainbuf, sparebuf, dma_wait_mode , NFC_PAGE_MODE);
				NFC_RandomDisable();
				if(ret == -ERR_ECC)
					ret = NFC_Read_Spare(cmd_list, readop->mainbuf, sparebuf, dma_wait_mode , NFC_PAGE_MODE);
				
				/**************************************************************************************  
				* 1. add by Neil, from v2.09
				* 2. if spare area is all 0xff in random disable mode, it means the page is a clear page
				* 3. because in toshiba 24nm nand, too many clear pages are not all 0xff
				***************************************************************************************/
				if((ret == -ERR_ECC)&&(sparebuf[0]==0xff)&&(sparebuf[1]==0xff)&&(sparebuf[2]==0xff)&&(sparebuf[3]==0xff)&&(sparebuf[4]==0xff)&&(sparebuf[5]==0xff)&&(sparebuf[6]==0xff)&&(sparebuf[7]==0xff))
				{
					//PHY_DBG("[Read_single_page_spare] find not all 0xff clear page!  chip = %d, block = %d, page = %d\n", readop->chip, readop->block, readop->page);
					ret = 0;
				}
				
			}
			else
			{
				ret = NFC_Read_Spare(cmd_list, readop->mainbuf, sparebuf, dma_wait_mode , NFC_PAGE_MODE);
			}

			if((ret != -ERR_ECC)||(k==READ_RETRY_CYCLE))
			{
			    if((READ_RETRY_MODE>=0x10)&&(READ_RETRY_MODE<0x20))  //toshiba mode
			    {
    			    //exit toshiba readretry
    				PHY_ResetChip(readop->chip);
			    }
			    else if((READ_RETRY_MODE>=0x20)&&(READ_RETRY_MODE<0x30))   //samsung mode
			    {
			        NFC_SetDefaultParam(readop->chip, default_value, READ_RETRY_TYPE);
			    } 
			    
				break;
			}

			RetryCount[readop->chip]++;    				    				
		}

    	if(k>0)
    	{
    		PHY_DBG("[Read_single_page_spare] NFC_ReadRetry %d cycles, chip = %d, block = %d, page = %d, RetryCount = %d  \n", k ,readop->chip,readop->block, readop->page, RetryCount[readop->chip]);
    		if(ret == -ERR_ECC)
    		    PHY_DBG("ecc error!\n");
    		PHY_DBG("spare buf: %x, %x, %x, %x, %x, %x, %x, %x\n", sparebuf[0],sparebuf[1],sparebuf[2],sparebuf[3],sparebuf[4],sparebuf[5],sparebuf[6],sparebuf[7]);
    	}
    	
    	if(ret == ECC_LIMIT)
    		ret = 0;
        
        
    }
    else 
    {
		if(SUPPORT_RANDOM)
        {
			random_seed = _cal_random_seed(readop->page);
			NFC_SetRandomSeed(random_seed);
			NFC_RandomEnable();
			ret = NFC_Read_Spare(cmd_list, readop->mainbuf, sparebuf, dma_wait_mode , NFC_PAGE_MODE);
			NFC_RandomDisable();
			if(ret == -ERR_ECC)
				ret = NFC_Read_Spare(cmd_list, readop->mainbuf, sparebuf, dma_wait_mode , NFC_PAGE_MODE);
		}
		else
		{
			ret = NFC_Read_Spare(cmd_list, readop->mainbuf, sparebuf, dma_wait_mode , NFC_PAGE_MODE);
		}	

    }

	if (dma_wait_mode)
		_pending_dma_irq_sem();

	if (readop->oobbuf){
		MEMCPY(readop->oobbuf,sparebuf, 2 * 4);
	}

	NFC_DeSelectChip(readop->chip);
	NFC_DeSelectRb(rb);

	//FREE(sparebuf);
	return ret;
}
/*
************************************************************************************************************************
*                       READ NAND FLASH PHYSICAL PAGE DATA
*
*Description: Read a page from a nand flash physical page to buffer.
*
*Arguments  : pPageAdr      the po__s32er to the accessed page parameter.
*
*Return     : the result of physical page read;
*               = 0     read physical page successful;
*               > 0     read physical page successful, but need do some process;
*               < 0     read physical page failed.
************************************************************************************************************************
*/


__s32 PHY_SimpleRead_Seq (struct boot_physical_param *readop)
{
	return(_read_single_page_seq(readop,0));
}
__s32 PHY_SimpleRead_1K (struct boot_physical_param *readop)
{
	return(_read_single_page_1K(readop,0));
}


__s32  PHY_BlockErase(struct __PhysicOpPara_t *pBlkAdr)
{
	__u32 chip;	
	__u32 rb;
	__u8 addr[4][5];
	__s32 ret;	
	__u32 plane_cnt,i,list_len;
	__u32 block_in_chip;
	NFC_CMD_LIST cmd_list[8];

	/*get chip no*/
	chip = _cal_real_chip(pBlkAdr->BankNum);
	  rb = _cal_real_rb(chip);
	if (0xff == chip){
		PHY_ERR("PHY_BlockErase : beyond chip count\n");
		return -ERR_INVALIDPHYADDR;
	}
	/*get block no within chip*/
	block_in_chip = _cal_block_in_chip(pBlkAdr->BankNum,pBlkAdr->BlkNum);
	if (0xffffffff == block_in_chip){
		PHY_ERR("PHY_BlockErase : beyond block of per chip  count\n");
		return -ERR_INVALIDPHYADDR;
	}
		
	/*create cmd list*/	
	plane_cnt = SUPPORT_MULTI_PROGRAM ? PLANE_CNT_OF_DIE : 1;	
	
	for (i = 0; i < plane_cnt; i++){
		_cal_addr_in_chip(block_in_chip+ i*MULTI_PLANE_BLOCK_OFFSET,0, 0,addr[i], 3);
		_add_cmd_list(cmd_list+i,0x60,3,addr[i],NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE);
	
	}
	_add_cmd_list(cmd_list + i,0xd0,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE);
	list_len = plane_cnt + 1;	
	for(i = 0; i < list_len - 1; i++){
		cmd_list[i].next = &(cmd_list[i+1]);
	}
	_wait_rb_ready_int(chip);
	NFC_SelectChip(chip);
	NFC_SelectRb(rb);

	ret = NFC_Erase(cmd_list, SUPPORT_RB_IRQ);	
	NFC_DeSelectChip(chip);
	NFC_DeSelectRb(rb);
	
	if (ret == -ERR_TIMEOUT)
		PHY_ERR("PHY_BlockErase : erase timeout\n");
	return ret;
}

__s32 _read_sectors(struct boot_physical_param *readop,__u8 dma_wait_mode)
{
       __s32 ret;
	__u32  k;
	__u32 rb;
	__u32 random_seed;
	__u8 sparebuf[4*16];
	__u8 default_value[16];
	__u8 addr[5];
	NFC_CMD_LIST cmd_list[4];
	__u32 list_len,i;
	__u8 *data_buf;

	data_buf = PageCachePool.TmpPageCache;
	//data_buf = (__u8 *)MALLOC(SECTOR_CNT_OF_SINGLE_PAGE*512);
	
	//sparebuf = (__u8 *)MALLOC(SECTOR_CNT_OF_SINGLE_PAGE * 4);	
	/*create cmd list*/
	/*samll block*/
	if (SECTOR_CNT_OF_SINGLE_PAGE == 1){		
		_cal_addr_in_chip(readop->block,readop->page,0,addr,4);
		_add_cmd_list(cmd_list,0x00,4,addr,NFC_DATA_FETCH,NFC_IGNORE,NFC_IGNORE,NFC_WAIT_RB);
	}
	/*large block*/
	else{
		/*the cammand have no corresponding feature if IGNORE was set, */		
		_cal_addr_in_chip(readop->block,readop->page,0,addr,5);
		_add_cmd_list(cmd_list,0x00,5,addr,NFC_NO_DATA_FETCH,NFC_IGNORE,NFC_IGNORE,NFC_NO_WAIT_RB);
		
	}	
	_add_cmd_list(cmd_list + 1,0x05,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE);
	_add_cmd_list(cmd_list + 2,0xe0,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE);
	_add_cmd_list(cmd_list + 3,0x30,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE);
	list_len = 4;
	for(i = 0; i < list_len - 1; i++){
		cmd_list[i].next = &(cmd_list[i+1]);
	}

	/*wait rb ready*/
	ret = _wait_rb_ready(readop->chip);
	if (ret)
		return ret;

	rb = _cal_real_rb(readop->chip);
	NFC_SelectChip(readop->chip);
	NFC_SelectRb(rb);

    if(SUPPORT_READ_RETRY)
    {
    	if((READ_RETRY_MODE>=0x10)&&(READ_RETRY_MODE<0x30))  //toshiba & samsung mode
            RetryCount[readop->chip] = 0;
    	
        for( k = 0; k<(READ_RETRY_CYCLE+1);k++)
		{

			if(RetryCount[readop->chip]==(READ_RETRY_CYCLE+1))
				RetryCount[readop->chip] = 0;

			if(k>0)
			{
			    if(NFC_ReadRetry(readop->chip,RetryCount[readop->chip], READ_RETRY_TYPE))
			    {
			        PHY_ERR("[Read_sectors] NFC_ReadRetry fail \n");
			        return -1;
			    }
			}
			
			if(SUPPORT_RANDOM)
			{
				random_seed = _cal_random_seed(readop->page);
				NFC_SetRandomSeed(random_seed);
				NFC_RandomEnable();
				ret = NFC_Read(cmd_list, data_buf, sparebuf, dma_wait_mode , NFC_PAGE_MODE);
				NFC_RandomDisable();
				if(ret == -ERR_ECC)
					ret = NFC_Read(cmd_list, data_buf, sparebuf, dma_wait_mode , NFC_PAGE_MODE);
				
					/**************************************************************************************  
					* 1. add by Neil, from v2.09
					* 2. if spare area is all 0xff in random disable mode, it means the page is a clear page
					* 3. because in toshiba 24nm nand, too many clear pages are not all 0xff
					***************************************************************************************/
					if((ret == -ERR_ECC)&&(sparebuf[0]==0xff)&&(sparebuf[1]==0xff)&&(sparebuf[2]==0xff)&&(sparebuf[3]==0xff)&&(sparebuf[4]==0xff)&&(sparebuf[5]==0xff)&&(sparebuf[6]==0xff)&&(sparebuf[7]==0xff))
					{
						//PHY_DBG("[Read_sectors] find not all 0xff clear page!  chip = %d, block = %d, page = %d\n", readop->chip, readop->block, readop->page);
						ret = 0;
					}
			}
			else
			{
				ret = NFC_Read(cmd_list, data_buf, sparebuf, dma_wait_mode , NFC_PAGE_MODE);
			}
			
			if((ret != -ERR_ECC)||(k==(READ_RETRY_CYCLE)))
			{
			    if((READ_RETRY_MODE>=0x10)&&(READ_RETRY_MODE<0x20))  //toshiba mode
			    {
    			    //exit toshiba readretry
    				PHY_ResetChip(readop->chip);
			    } 
			    else if((READ_RETRY_MODE>=0x20)&&(READ_RETRY_MODE<0x30))   //samsung mode
			    {
			        NFC_SetDefaultParam(readop->chip, default_value, READ_RETRY_TYPE);
			    }
			    
				break;
			}

			 RetryCount[readop->chip]++;
				
				
		}
         
        if(k>0)
    	{
    		PHY_DBG("[Read_sectors] NFC_ReadRetry %d cycles, chip = %d, block = %d, page = %d, RetryCount = %d  \n", k ,readop->chip,readop->block, readop->page, RetryCount[readop->chip]);
    		if(ret == -ERR_ECC)
    		    PHY_DBG("ecc error!\n");
    		PHY_DBG("spare buf: %x, %x, %x, %x, %x, %x, %x, %x\n", sparebuf[0],sparebuf[1],sparebuf[2],sparebuf[3],sparebuf[4],sparebuf[5],sparebuf[6],sparebuf[7]);
    	}
    	
	    if(ret == ECC_LIMIT)
		    ret =0;

    }
    else
    {

		if(SUPPORT_RANDOM)
        {
			random_seed = _cal_random_seed(readop->page);
			NFC_SetRandomSeed(random_seed);
			NFC_RandomEnable();
			ret = NFC_Read(cmd_list, data_buf, sparebuf, dma_wait_mode , NFC_PAGE_MODE);
			NFC_RandomDisable();
			if(ret == -ERR_ECC)
				ret = NFC_Read(cmd_list, data_buf, sparebuf, dma_wait_mode , NFC_PAGE_MODE);

		}
		else
		{
			ret = NFC_Read(cmd_list, data_buf, sparebuf, dma_wait_mode , NFC_PAGE_MODE);
		}

        
    }

	if (dma_wait_mode)
		_pending_dma_irq_sem();
	
	if (readop->oobbuf){
		MEMCPY(readop->oobbuf,sparebuf, 2 * 4);
	}

	for (i = 0; i < SECTOR_CNT_OF_SINGLE_PAGE; i++){
		if (readop->sectorbitmap & (1 << i)){
			MEMCPY( (__u8 *)readop->mainbuf+i*512,data_buf+i*512, 512);
			}
		}

	NFC_DeSelectChip(readop->chip);
	NFC_DeSelectRb(rb);
	
	//FREE(data_buf,SECTOR_CNT_OF_SINGLE_PAGE*512);
	return ret;


	
}

/*********************************************************************
**************************two plane read operation***********************
***********************************************************************/
__s32 _two_plane_read(struct __PhysicOpPara_t *pPageAdr,__u8 dma_wait_mode)
{
	__u8 addr[2][5];
	__s32 ret;
	__u32 chip;
	__u32 rb;
	__u32 list_len,i,plane_cnt;
	__u32 bitmap_in_single_page;
	__u32 block_in_chip;
	
	NFC_CMD_LIST cmd_list[8];
	struct boot_physical_param readop;

	/*get chip no*/
	chip = _cal_real_chip(pPageAdr->BankNum);
	if (0xff == chip){
		PHY_ERR("PHY_PageRead : beyond chip count\n");
		return -ERR_INVALIDPHYADDR;
	}
	/*get block no within chip*/
	block_in_chip = _cal_block_in_chip(pPageAdr->BankNum,pPageAdr->BlkNum);
	if (0xffffffff == block_in_chip){
		PHY_ERR("PHY_PageRead : beyond block of per chip  count\n");
		return -ERR_INVALIDPHYADDR;
	}	
	plane_cnt = SUPPORT_MULTI_PROGRAM ? PLANE_CNT_OF_DIE : 1;	

	/*create cmd list*/
	/*send 0x60+3addr - 0x60 + 3addr - 0x30 for samsung 4k page*/
	for(i = 0; i< plane_cnt; i++){
		_cal_addr_in_chip(block_in_chip+i*MULTI_PLANE_BLOCK_OFFSET, pPageAdr->PageNum,0,addr[i],3);
		_add_cmd_list(cmd_list+i, 0x60,3,addr[i],NFC_NO_DATA_FETCH,NFC_IGNORE, NFC_IGNORE,NFC_IGNORE);
	}		
	_add_cmd_list(cmd_list+i,0x30, 0,NFC_IGNORE,NFC_NO_DATA_FETCH,NFC_IGNORE, NFC_IGNORE,NFC_WAIT_RB);
	list_len = plane_cnt + 1;
	for(i = 0; i < list_len - 1; i++){
		cmd_list[i].next = &(cmd_list[i+1]);
	}
	rb = _cal_real_rb(chip);
	NFC_SelectChip(chip);
	NFC_SelectRb(rb);
	ret = _wait_rb_ready(chip);
	if (ret)
		return ret;
	
	ret = NFC_Read(cmd_list, NULL, NULL, 0,NFC_NORMAL_MODE);

	/*send 0x00 +5addr --(05+2addr-e0...)*/
	for (i = 0; i < plane_cnt; i++){
		/*init single page operation param*/
		readop.chip = chip;
		readop.block = block_in_chip+ i*MULTI_PLANE_BLOCK_OFFSET;
		readop.page = pPageAdr->PageNum;		
		readop.mainbuf = (__u8 *)(pPageAdr->MDataPtr) + 512*SECTOR_CNT_OF_SINGLE_PAGE*i;
		if (pPageAdr->SDataPtr)
			readop.oobbuf = (__u8 *)(pPageAdr->SDataPtr) + 4*SECTOR_CNT_OF_SINGLE_PAGE*i;

	
		bitmap_in_single_page = FULL_BITMAP_OF_SINGLE_PAGE &
			(pPageAdr->SectBitmap >> (i*SECTOR_CNT_OF_SINGLE_PAGE));
		readop.sectorbitmap = bitmap_in_single_page;		
		ret |= _read_sectors(&readop,dma_wait_mode);
	}	
	NFC_DeSelectChip(chip);
	NFC_DeSelectRb(rb);
	return ret;
}
/*
************************************************************************************************************************
*                       READ NAND FLASH PHYSICAL PAGE DATA
*
*Description: Read a page from a nand flash physical page to buffer.
*
*Arguments  : pPageAdr      the po__s32er to the accessed page parameter.
*
*Return     : the result of physical page read;
*               = 0     read physical page successful;
*               > 0     read physical page successful, but need do some process;
*               < 0     read physical page failed.
************************************************************************************************************************
*/
__s32  PHY_PageRead(struct __PhysicOpPara_t *pPageAdr)
{
	__s32 ret;
	__u32 chip;
	__u32 block_in_chip;
	__u32 plane_cnt,i;	
	__u32 bitmap_in_single_page;
	struct boot_physical_param readop;	
	
	/*create cmd list*/	
	if (SUPPORT_MULTI_READ){
	/*two plane read */
		ret = _two_plane_read(pPageAdr,SUPPORT_DMA_IRQ);
		goto PHY_PageRead_exit;
	}

	ret = 0;
	plane_cnt = SUPPORT_MULTI_PROGRAM ? PLANE_CNT_OF_DIE : 1;	
	
	/*get chip no*/
	chip = _cal_real_chip(pPageAdr->BankNum);
	if (0xff == chip){
		PHY_ERR("PHY_PageRead : beyond chip count\n");
		return -ERR_INVALIDPHYADDR;
	}
	/*get block no within chip*/
	block_in_chip = _cal_block_in_chip(pPageAdr->BankNum,pPageAdr->BlkNum);
	if (0xffffffff == block_in_chip){
		PHY_ERR("PHY_PageRead : beyond block of per chip  count\n");
		return -ERR_INVALIDPHYADDR;
	}

	for (i = 0; i < plane_cnt; i++){
		/*init single page operation param*/
		readop.chip = chip;
		readop.block = block_in_chip  + i*MULTI_PLANE_BLOCK_OFFSET;
		readop.page = pPageAdr->PageNum;

			readop.mainbuf = (__u8 *)(pPageAdr->MDataPtr) + 512*SECTOR_CNT_OF_SINGLE_PAGE*i;
		    if (pPageAdr->SDataPtr)
			    readop.oobbuf = (__u8 *)(pPageAdr->SDataPtr) + 4*SECTOR_CNT_OF_SINGLE_PAGE*i;
		    else
			    readop.oobbuf = NULL;

		bitmap_in_single_page = FULL_BITMAP_OF_SINGLE_PAGE &
			(pPageAdr->SectBitmap >> (i*SECTOR_CNT_OF_SINGLE_PAGE));
		readop.sectorbitmap = bitmap_in_single_page;
			
		if (bitmap_in_single_page){
		/*bitmap of this plane is valid */
			if(bitmap_in_single_page == FULL_BITMAP_OF_SINGLE_PAGE)
			/*align page, use page mode */
				ret |= _read_single_page(&readop,SUPPORT_DMA_IRQ);
			else
			/*not align page , normal mode*/
				ret |= _read_sectors(&readop,SUPPORT_DMA_IRQ);
		}				
	}
	
PHY_PageRead_exit:
	if (ret == -ERR_TIMEOUT)
		PHY_ERR("PHY_PageRead : read timeout\n");
	if (ret == -ERR_ECC)
	{	
		PHY_ERR("PHY_PageRead : too much ecc err,bank %x block %x,page %x \n",pPageAdr->BankNum,pPageAdr->BlkNum,
					pPageAdr->PageNum);
		ret = 0;
	}

	if(ret == ECC_LIMIT)
	{
		PHY_ERR("%s : %d : ecc limit\n",__FUNCTION__,__LINE__);
	}
	return ret;
}


void _cal_addr_in_chip_for_spare(__u32 block, __u32 page, __u32 sector,__u8 *addr, __u8 cycle)
{
	__u32 row;
	__u32 column;
	#if 0
	__u32 ecc_size;

	if(NandStorageInfo.EccMode == 0)
	  ecc_size = 32;
	else if(NandStorageInfo.EccMode == 1)
	  ecc_size = 53;
	else if(NandStorageInfo.EccMode == 2)
	  ecc_size = 60;
	else
	  ecc_size = 32;
	#endif
	
	//column = (1024 + ecc_size)* (sector>>1);
	column = 1024* (sector>>1);
	row = block * PAGE_CNT_OF_PHY_BLK + page;
	
	switch(cycle){
		case 1:
			addr[0] = 0x00;
			break;
		case 2:			
			addr[0] = column & 0xff;
			addr[1] = (column >> 8) & 0xff;
			break;
		case 3:			
			addr[0] = row & 0xff;
			addr[1] = (row >> 8) & 0xff;
			addr[2] = (row >> 16) & 0xff;
			break;
		case 4:
			addr[0] = column && 0xff;
			addr[1] = (column >> 8) & 0xff;
			addr[2] = row & 0xff;
			addr[3] = (row >> 8) & 0xff;
			break;
		case 5:
			addr[0] = column & 0xff;
			addr[1] = (column >> 8) & 0xff;
			addr[2] = row & 0xff;
			addr[3] = (row >> 8) & 0xff;
			addr[4] = (row >> 16) & 0xff;
			break;
		default:
			break;
	}
	
}



__s32 _read_sectors_for_spare(struct boot_physical_param *readop,__u8 dma_wait_mode)
{
	__u8 sparebuf[4*2];		
	__u8 default_value[16];
	__u8 addr[5];
	__u8 addr1[2],addr2[2];
	__u32 column;
	__u32 list_len,i,j, k;
	__u32 rb;
	__s32 ret, ret1 , free_page_flag, read_retry_exit_flag;
	__u32 ecc_size;
	__s32 random_seed;
	NFC_CMD_LIST cmd_list[4];

	if(NandStorageInfo.EccMode == 0)
	  ecc_size = 32;
	else if(NandStorageInfo.EccMode == 1)
	  ecc_size = 46;
	else if(NandStorageInfo.EccMode == 2)
	  ecc_size = 54;
	else if(NandStorageInfo.EccMode == 3)
	  ecc_size = 60;	
	else
	  ecc_size = 32;

	for(i=0; i<8; i++)
		sparebuf[i] = 0x3e;

	ret = 0;
	ret1 = 0;
	read_retry_exit_flag = 0;
	
	_cal_addr_in_chip(readop->block,readop->page, 0, addr, 5);
	_add_cmd_list(cmd_list,0x00,5,addr, NFC_NO_DATA_FETCH,NFC_IGNORE,NFC_IGNORE, NFC_IGNORE);
	if (SUPPORT_MULTI_READ){
	/*send 0x00 + 5addr*/
		list_len = 1;
	}
	else{
	/*send 0x00 + 5addr --- 0x30*/
		list_len = 2;	
		_add_cmd_list(cmd_list + 1,0x30,0,NFC_IGNORE,NFC_IGNORE, NFC_IGNORE,NFC_IGNORE,NFC_WAIT_RB);
	}
	
	for(i = 0; i < list_len - 1; i++){
		cmd_list[i].next = &(cmd_list[i+1]);
	}

	/*wait rb ready*/
	ret = _wait_rb_ready(readop->chip);
	if (ret)
		return ret;

	rb = _cal_real_rb(readop->chip);
	NFC_SelectChip(readop->chip);
	NFC_SelectRb(rb);
	
	ret = NFC_Read(cmd_list, NULL, NULL, 0 , NFC_NORMAL_MODE);

	/*send 05 + 2addr- e0 get 512 byte data*/
	list_len = 4;
	for (i = 0; i < 2; i++){
		if (readop->sectorbitmap & (1 << i)){

			/*get main data addr*/
			_cal_addr_in_chip_for_spare(0,0,i<<1,addr1,2);
			/*get spare data addr*/
			//column = (1024 + ecc_size)*i +1024;
			column = 512*SECTOR_CNT_OF_SINGLE_PAGE + i*ecc_size;
			addr2[0] = column & 0xff;
			addr2[1] = (column >> 8) & 0xff;

			_add_cmd_list(cmd_list, 0x05, 2, addr1,NFC_NO_DATA_FETCH,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE);
			_add_cmd_list(cmd_list+1,0xe0, 0,NFC_IGNORE,NFC_DATA_FETCH,NFC_MAIN_DATA_FETCH,1024,NFC_NO_WAIT_RB);
			_add_cmd_list(cmd_list+2,0x05,2,addr2,NFC_NO_DATA_FETCH,NFC_IGNORE,NFC_IGNORE,NFC_IGNORE);
			_add_cmd_list(cmd_list+3,0xe0, 0,NFC_IGNORE,NFC_DATA_FETCH,NFC_SPARE_DATA_FETCH,NFC_IGNORE,NFC_NO_WAIT_RB);
			for(j = 0; j < list_len - 1; j++){
				cmd_list[j].next = &(cmd_list[j+1]);
			}
			if (_wait_rb_ready(readop->chip))
				return ERR_TIMEOUT;

	        if(SUPPORT_READ_RETRY)
	        {
                if((READ_RETRY_MODE>=0x10)&&(READ_RETRY_MODE<0x30))  //toshiba & samsung mode
                    RetryCount[readop->chip] = 0;
                    
                for( k = 0; k<(READ_RETRY_CYCLE+1);k++)
	    		{
	    			if(RetryCount[readop->chip]==(READ_RETRY_CYCLE+1))
	    				RetryCount[readop->chip] = 0;
	    
	    			if(k>0)
	    			{
	    			    if(NFC_ReadRetry(readop->chip,RetryCount[readop->chip],READ_RETRY_TYPE))
	    			    {
	    			        PHY_ERR("[Read_sectors] NFC_ReadRetry fail \n");
	    			        return -1;
	    			    }
	    			}
	    			
					if(SUPPORT_RANDOM)
	    			{
	    				random_seed = _cal_random_seed(readop->page);
	    				NFC_SetRandomSeed(random_seed);
	    				NFC_RandomEnable();
	    				free_page_flag = 0;
	    				ret1 = NFC_Read(cmd_list, (__u8 *)(readop->mainbuf)+i*1024,sparebuf+4*i, dma_wait_mode , NFC_NORMAL_MODE);
	    				if(readop->page == 0)
    					{
    						if((sparebuf[4*i] == 0x0a)&&(sparebuf[4*i+1] == 0x53)&&(sparebuf[4*i+2] == 0xb8)&&(sparebuf[4*i+3] == 0xc2))
    							free_page_flag = 1;
    
    					}
    					else if((readop->page%128) == 127)
    					{
    						if((sparebuf[4*i] == 0x32)&&(sparebuf[4*i+1] == 0x43)&&(sparebuf[4*i+2] == 0xaa)&&(sparebuf[4*i+3] == 0x4e))
    							free_page_flag = 1;
    			
    					}
    
    					if(free_page_flag)
    					{
    						ret1 = 0;
    						sparebuf[4*i] = 0xff;
    						sparebuf[4*i+1] = 0xff;
    						sparebuf[4*i+2] = 0xff;
    						sparebuf[4*i+3] = 0xff;
    					}
	    				NFC_RandomDisable();
	    				if(ret1 == -ERR_ECC)
	    					ret1 = NFC_Read(cmd_list, (__u8 *)(readop->mainbuf)+i*1024,sparebuf+4*i, dma_wait_mode , NFC_NORMAL_MODE);
	    				
	    			    	/**************************************************************************************  
							* 1. add by Neil, from v2.09
							* 2. if spare area is all 0xff in random disable mode, it means the page is a clear page
							* 3. because in toshiba 24nm nand, too many clear pages are not all 0xff
							***************************************************************************************/
							if((ret1 == -ERR_ECC)&&(sparebuf[4*i]==0xff)&&(sparebuf[4*i+1]==0xff)&&(sparebuf[4*i+2]==0xff)&&(sparebuf[4*i+3]==0xff))
							{
								//PHY_DBG("[Read_sectors_for_spare] find not all 0xff clear page!  chip = %d, block = %d, page = %d\n, i = %d", readop->chip, readop->block, readop->page, i);
								ret1 = 0;
							}
	    			}
	    			else
	    			{
	    				ret1 = NFC_Read(cmd_list, (__u8 *)(readop->mainbuf)+i*1024,sparebuf+4*i, dma_wait_mode , NFC_NORMAL_MODE);
	    			}

	    			if((ret1 != -ERR_ECC)||(k==(READ_RETRY_CYCLE)))
	    			{
	    			    if((READ_RETRY_MODE>=0x10)&&(READ_RETRY_MODE<0x20))  //toshiba mode
	    			    {
    	    			    //exit toshiba readretry
    	    			    read_retry_exit_flag = 1;
	    			    }
	    			    else if((READ_RETRY_MODE>=0x20)&&(READ_RETRY_MODE<0x30))   //samsung mode
        			    {
        			        NFC_SetDefaultParam(readop->chip, default_value, READ_RETRY_TYPE);
        			    }
	    			    
	    				break;
	    			}
	    
	    			RetryCount[readop->chip]++;	
	    		}

    		    if(k>0)
            	{
            		PHY_DBG("[Read_sectors_for_spare] NFC_ReadRetry %d cycles, chip = %d, block = %d, page = %d, RetryCount = %d, i=%d  \n", k ,readop->chip,readop->block, readop->page, RetryCount[readop->chip], i);
            		if(ret1 == -ERR_ECC)
            		    PHY_DBG("ecc error!\n");
            		PHY_DBG("spare buf: %x, %x, %x, %x\n", sparebuf[4*i],sparebuf[4*i+1],sparebuf[4*i+2],sparebuf[4*i+3]);
            	}
            	
    			if(ret1 == ECC_LIMIT)
    				ret1 = 0;
	        }
	        else
	        {
				if(SUPPORT_RANDOM)
	            {
	    			random_seed = _cal_random_seed(readop->page);
	    			NFC_SetRandomSeed(random_seed);
	    			NFC_RandomEnable();
					free_page_flag = 0;
	    			ret1 = NFC_Read(cmd_list, (__u8 *)(readop->mainbuf)+i*1024,sparebuf+4*i, dma_wait_mode , NFC_NORMAL_MODE);
					if(readop->page == 0)
					{
						if((sparebuf[4*i] == 0x0a)&&(sparebuf[4*i+1] == 0x53)&&(sparebuf[4*i+2] == 0xb8)&&(sparebuf[4*i+3] == 0xc2))
							free_page_flag = 1;

					}
					else if((readop->page%128) == 127)
					{
						if((sparebuf[4*i] == 0x32)&&(sparebuf[4*i+1] == 0x43)&&(sparebuf[4*i+2] == 0xaa)&&(sparebuf[4*i+3] == 0x4e))
							free_page_flag = 1;
			
					}

					if(free_page_flag)
					{
						ret1 = 0;
						sparebuf[4*i] = 0xff;
						sparebuf[4*i+1] = 0xff;
						sparebuf[4*i+2] = 0xff;
						sparebuf[4*i+3] = 0xff;
					}
	    			NFC_RandomDisable();
	    			if(ret1 == -ERR_ECC)
	    				ret1 = NFC_Read(cmd_list, (__u8 *)(readop->mainbuf)+i*1024,sparebuf+4*i, dma_wait_mode , NFC_NORMAL_MODE);
				}
	    		else
	    		{
	    			ret1 = NFC_Read(cmd_list, (__u8 *)(readop->mainbuf)+i*1024,sparebuf+4*i, dma_wait_mode , NFC_NORMAL_MODE);
	    		}

	            
	        }

			ret |= ret1;
			
			if (dma_wait_mode)
				_pending_dma_irq_sem();
			
			if (readop->oobbuf){
				MEMCPY((__u8 *)(readop->oobbuf)+4*i,sparebuf+4*i,4);
			}			
		}
					
	}
	
	if(read_retry_exit_flag)
	    PHY_ResetChip(readop->chip);
	    
	NFC_DeSelectChip(readop->chip);
	NFC_DeSelectRb(rb);
	
	return ret;
}



__s32  PHY_PageReadSpare(struct __PhysicOpPara_t *pPageAdr)
{
	__s32 ret;
	__u32 chip;
	__u32 block_in_chip;
	__u32 plane_cnt,i;	
	__u32 bitmap_in_single_page;
	struct boot_physical_param readop;	
	
	
	ret = 0;
	plane_cnt = SUPPORT_MULTI_PROGRAM ? PLANE_CNT_OF_DIE : 1;	
	
	/*get chip no*/
	chip = _cal_real_chip(pPageAdr->BankNum);
	if (0xff == chip){
		PHY_ERR("PHY_PageRead : beyond chip count\n");
		return -ERR_INVALIDPHYADDR;
	}
	/*get block no within chip*/
	block_in_chip = _cal_block_in_chip(pPageAdr->BankNum,pPageAdr->BlkNum);
	if (0xffffffff == block_in_chip){
		PHY_ERR("PHY_PageRead : beyond block of per chip  count\n");
		return -ERR_INVALIDPHYADDR;
	}

	for (i = 0; i < plane_cnt; i++){
		/*init single page operation param*/
		readop.chip = chip;
		readop.block = block_in_chip  + i*MULTI_PLANE_BLOCK_OFFSET;
		readop.page = pPageAdr->PageNum;

		readop.mainbuf = (__u8 *)(pPageAdr->MDataPtr) + 512*SECTOR_CNT_OF_SINGLE_PAGE*i;
	    if (pPageAdr->SDataPtr)
		    readop.oobbuf = (__u8 *)(pPageAdr->SDataPtr) + 4*SECTOR_CNT_OF_SINGLE_PAGE*i;
	    else
		    readop.oobbuf = NULL;

		bitmap_in_single_page = FULL_BITMAP_OF_SINGLE_PAGE &
			(pPageAdr->SectBitmap >> (i*SECTOR_CNT_OF_SINGLE_PAGE));
		readop.sectorbitmap = bitmap_in_single_page;
			
		if (bitmap_in_single_page){
		/*bitmap of this plane is valid */
			if(bitmap_in_single_page == FULL_BITMAP_OF_SINGLE_PAGE)
			/*align page, use page mode */
				ret |= _read_single_page(&readop,SUPPORT_DMA_IRQ);
			else
			/*not align page , normal mode*/
				ret |= _read_single_page_spare(&readop,SUPPORT_DMA_IRQ);
		}				
	}
	
	if (ret == -ERR_TIMEOUT)
		PHY_ERR("PHY_PageReadSpare : read timeout\n");
	if (ret == -ERR_ECC)
	{	
		PHY_ERR("PHY_PageReadSpare : too much ecc err,bank %x block %x,page %x \n",pPageAdr->BankNum,pPageAdr->BlkNum,
					pPageAdr->PageNum);
		ret = 0;
	}

	if(ret == ECC_LIMIT)
	{
		PHY_ERR("%s : %d : ecc limit\n",__FUNCTION__,__LINE__);
	}
	return ret;
}

void PHY_FreePageCheck(struct __PhysicOpPara_t  *pPageAdr)
{
	__u8 oob[8];
	__u32 i;
	
	struct __PhysicOpPara_t  param = *pPageAdr;
	param.MDataPtr = (__u8 *)MALLOC(1024);
	param.SDataPtr = oob;	
	param.SectBitmap = 0x3;

	PHY_PageRead(&param);
	for(i = 0; i < 8; i++)
	{	
		if( (i%4 != 3) && (oob[i] != 0xff))
		{
			PRINT("%s : sorry, you can not write in dirty page\n",__FUNCTION__);
			PRINT("oob data : --%x--%x--%x--%x--%x--%x--\n",
				oob[0],oob[1],oob[2],oob[4],oob[5],oob[6]);
			while(1);
		}
	}
	
	FREE(param.MDataPtr,1024);
}

/*
************************************************************************************************************************
*                       WRITE NAND FLASH PHYSICAL PAGE DATA
*
*Description: Write a page from buffer to a nand flash physical page.
*
*Arguments  : pPageAdr      the po__s32er to the accessed page parameter.
*
*Return     : The result of the page write;
*               = 0     page write successful;
*               > 0     page write successful, but need do some process;
*               < 0     page write failed.
************************************************************************************************************************
*/
__s32  PHY_PageWrite(struct __PhysicOpPara_t  *pPageAdr)
{
	__s32 ret;
	__u32 chip;
	__u32 block_in_chip;
	__u32 plane_cnt,i;	
	__u32 program1,program2;
	struct boot_physical_param writeop;

	//PHY_FreePageCheck(pPageAdr);
	
	ret = 0;
	plane_cnt = SUPPORT_MULTI_PROGRAM ? PLANE_CNT_OF_DIE : 1;	
	/*get chip no*/
	chip = _cal_real_chip(pPageAdr->BankNum);
	if (0xff == chip){
		PHY_ERR("PHY_PageWrite : beyond chip count\n");
		return -ERR_INVALIDPHYADDR;
	}	
	/*get block no within chip*/
	block_in_chip = _cal_block_in_chip(pPageAdr->BankNum,pPageAdr->BlkNum);
	if (0xffffffff == block_in_chip){
		PHY_ERR("PHY_PageWrite : beyond block of per chip  count\n");
		return -ERR_INVALIDPHYADDR;
	}

	//_wait_rb_ready(chip);
	 
	for (i = 0; i < plane_cnt; i++){
		if(i == 0)
			_wait_rb_ready_int(chip);
		else
		_wait_rb_ready(chip);
		
		/*init single page operation param*/
		writeop.chip = chip;
		writeop.block = block_in_chip + i*MULTI_PLANE_BLOCK_OFFSET;
		writeop.page = pPageAdr->PageNum;

		    writeop.mainbuf = (__u8 *)(pPageAdr->MDataPtr) + 512*SECTOR_CNT_OF_SINGLE_PAGE*i;
		    if (pPageAdr->SDataPtr)
			    writeop.oobbuf = (__u8 *)(pPageAdr->SDataPtr) + 4*SECTOR_CNT_OF_SINGLE_PAGE*i;		
		    else
			    writeop.oobbuf = NULL;

		
		writeop.sectorbitmap = FULL_BITMAP_OF_SINGLE_PAGE;
		if (i == 0){
			program1 = 0x80;
			if (SUPPORT_MULTI_PROGRAM)				
				program2 = NandStorageInfo.OptPhyOpPar.MultiPlaneWriteCmd[0];
			else
				program2 = 0x10;
		}
		else{
			program1 = NandStorageInfo.OptPhyOpPar.MultiPlaneWriteCmd[1];
			program2 = 0x10;
		}
		ret |= _write_signle_page(&writeop,program1,program2,SUPPORT_DMA_IRQ,SUPPORT_RB_IRQ);
		
	}
	if (ret == -ERR_TIMEOUT)
		PHY_ERR("PHY_PageWrite : write timeout\n");
	return ret;
}

__s32 _single_copy_back(struct __PhysicOpPara_t *pSrcPage, struct __PhysicOpPara_t *pDstPage)
{
	__u8 addr[5];
	__s32 ret;
	__u32 chip;
	__u32 rb;
	__u32 src_block_in_chip,dst_block_in_chip;
	__u32 list_len,i,addr_cycle;
	NFC_CMD_LIST cmd_list[8];

	/*get chip no*/
	chip = _cal_real_chip(pSrcPage->BankNum);
	if (0xff == chip){
		PHY_ERR("single_copy_back : beyond chip count\n");
		return -ERR_INVALIDPHYADDR;
	}
	/*get block no within chip*/
	src_block_in_chip = _cal_block_in_chip(pSrcPage->BankNum,pSrcPage->BlkNum);
	if (0xffffffff == src_block_in_chip){
		PHY_ERR("single_copy_back : beyond block of per chip  count\n");
		return -ERR_INVALIDPHYADDR;
	}
	dst_block_in_chip = _cal_block_in_chip(pDstPage->BankNum,pDstPage->BlkNum);
	if (0xffffffff == dst_block_in_chip){
		PHY_ERR("single_copy_back : beyond block of per chip  count\n");
		return -ERR_INVALIDPHYADDR;
	}	
	

	/*create cmd list*/
	addr_cycle = (SECTOR_CNT_OF_SINGLE_PAGE == 1) ?4:5;
	list_len = 2;
	/*copy back read*/
	_cal_addr_in_chip(src_block_in_chip,pSrcPage->PageNum,0,addr,addr_cycle);
	_add_cmd_list(cmd_list, 0x00,addr_cycle,addr,NFC_NO_DATA_FETCH,NFC_IGNORE, NFC_IGNORE,NFC_IGNORE);
	_add_cmd_list(cmd_list+1,0x35, 0,NFC_IGNORE,NFC_NO_DATA_FETCH,NFC_IGNORE, NFC_IGNORE,NFC_IGNORE);
	for(i = 0; i < list_len - 1; i++){
		cmd_list[i].next = &(cmd_list[i+1]);
	}
	
	_wait_rb_ready(chip);
	rb = _cal_real_rb(chip);
	NFC_SelectChip(chip);
	NFC_SelectRb(rb);
	ret = NFC_CopyBackRead(cmd_list);
	if (ret)
		return -1;
	/*copy back write*/
	_cal_addr_in_chip(dst_block_in_chip,pDstPage->PageNum,0,addr,addr_cycle);
	_add_cmd_list(cmd_list, 0x85,addr_cycle,addr,NFC_NO_DATA_FETCH,NFC_IGNORE, NFC_IGNORE,NFC_IGNORE);
	_add_cmd_list(cmd_list+1,0x10, 0,NFC_IGNORE,NFC_NO_DATA_FETCH,NFC_IGNORE, NFC_IGNORE,NFC_IGNORE);
	for(i = 0; i < list_len - 1; i++){
		cmd_list[i].next = &(cmd_list[i+1]);
	}
	ret = NFC_CopyBackWrite(cmd_list,0);

	NFC_DeSelectChip(chip);
	NFC_DeSelectRb(rb);
	return ret;
}

__s32 _multi_copy_back(struct __PhysicOpPara_t *pSrcPage, struct __PhysicOpPara_t *pDstPage)
{
	__u8 addr[2][5];
	__s32 ret;
	__u32 chip;
	__u32 rb;
	__u32 src_block_in_chip,dst_block_in_chip;
	__u32 list_len,i,addr_cycle,plane_cnt;
	__u32 program1,program2;
	NFC_CMD_LIST cmd_list[8];

	/*get chip no*/
	chip = _cal_real_chip(pSrcPage->BankNum);
	if (0xff == chip){
		PHY_ERR("single_copy_back : beyond chip count\n");
		return -ERR_INVALIDPHYADDR;
	}
	/*get block no within chip*/
	src_block_in_chip = _cal_block_in_chip(pSrcPage->BankNum,pSrcPage->BlkNum);
	if (0xffffffff == src_block_in_chip){
		PHY_ERR("single_copy_back : beyond block of per chip  count\n");
		return -ERR_INVALIDPHYADDR;
	}
	dst_block_in_chip = _cal_block_in_chip(pDstPage->BankNum,pDstPage->BlkNum);
	if (0xffffffff == dst_block_in_chip){
		PHY_ERR("single_copy_back : beyond block of per chip  count\n");
		return -ERR_INVALIDPHYADDR;
	}
	
	plane_cnt = SUPPORT_MULTI_PROGRAM ? PLANE_CNT_OF_DIE : 1;	
	/*create cmd list*/
	addr_cycle = (SECTOR_CNT_OF_SINGLE_PAGE == 1) ?4:5;
	/*copy back read*/
	if (NandStorageInfo.OptPhyOpPar.MultiPlaneCopyReadCmd[0] == 0x60){
	
		for(i = 0; i< plane_cnt; i++){
			_cal_addr_in_chip(src_block_in_chip+i*MULTI_PLANE_BLOCK_OFFSET, pSrcPage->PageNum,0,addr[i],addr_cycle);
			_add_cmd_list(cmd_list+i, 0x60,addr_cycle,addr[i],NFC_NO_DATA_FETCH,NFC_IGNORE, NFC_IGNORE,NFC_IGNORE);
		}		
		_add_cmd_list(cmd_list+i,0x35, 0,NFC_IGNORE,NFC_NO_DATA_FETCH,NFC_IGNORE, NFC_IGNORE,NFC_IGNORE);
		list_len = plane_cnt + 1;
	}
	else{
		
		for(i = 0; i< plane_cnt; i++){
			_cal_addr_in_chip(src_block_in_chip+i*MULTI_PLANE_BLOCK_OFFSET,pSrcPage->PageNum,0,addr[i],addr_cycle);
			_add_cmd_list(cmd_list+2*i, 0x00,addr_cycle,addr[i],NFC_NO_DATA_FETCH,NFC_IGNORE, NFC_IGNORE,NFC_IGNORE);
			_add_cmd_list(cmd_list+2*i+1,0x35, 0,NFC_IGNORE,NFC_NO_DATA_FETCH,NFC_IGNORE, NFC_IGNORE,NFC_IGNORE);			
		}
		list_len = plane_cnt * 2;	
	}
	for(i = 0; i < list_len - 1; i++){
		cmd_list[i].next = &(cmd_list[i+1]);
	}
	
	_wait_rb_ready(chip);
	rb = _cal_real_rb(chip);
	NFC_SelectChip(chip);
	NFC_SelectRb(rb);
	ret = NFC_CopyBackRead(cmd_list);
	if (ret)
		return -1;

	/*copy back write*/
	for(i = 0; i< plane_cnt; i++){
		_cal_addr_in_chip(dst_block_in_chip+i*MULTI_PLANE_BLOCK_OFFSET,pSrcPage->PageNum,0,addr[i],addr_cycle);
		if (i == 0){
			program1 = NandStorageInfo.OptPhyOpPar.MultiPlaneCopyWriteCmd[0];
			program2 = NandStorageInfo.OptPhyOpPar.MultiPlaneCopyWriteCmd[1];
		}
		else{
			program1 = NandStorageInfo.OptPhyOpPar.MultiPlaneCopyWriteCmd[2];
			program2 = 0x10;
		}
		_add_cmd_list(cmd_list+2*i, program1,addr_cycle,addr[i],NFC_NO_DATA_FETCH,NFC_IGNORE, NFC_IGNORE,NFC_IGNORE);
		_add_cmd_list(cmd_list+2*i+1,program2, 0,NFC_IGNORE,NFC_NO_DATA_FETCH,NFC_IGNORE, NFC_IGNORE,NFC_IGNORE);			
	}
	list_len = plane_cnt * 2;	
	for(i = 0; i < list_len - 1; i++){
		cmd_list[i].next = &(cmd_list[i+1]);
	}
	ret = NFC_CopyBackWrite(cmd_list,0);
	NFC_DeSelectChip(chip);
	NFC_DeSelectRb(rb);
	
	return ret;
	
}

/*
************************************************************************************************************************
*                           PHYSIC PAGE COPY-BACK
*
*Description: copy one physical page from one physical block to another physical block.
*
*Arguments  : pSrcPage      the parameter of the source page which need be copied;
*             pDstPage      the parameter of the destination page which copied to.
*
*Return     : the result of the page copy-back;
*               = 0         page copy-back successful;
*               = -1        page copy-back failed.
************************************************************************************************************************
*/
__s32  PHY_PageCopyback(struct __PhysicOpPara_t *pSrcPage, struct __PhysicOpPara_t *pDstPage)
{
	__s32 ret;

	if ( (!SUPPORT_PAGE_COPYBACK) || ((pSrcPage->PageNum + pDstPage->PageNum) & 0x1) ){
	/*cant not use copyback*/		
		pSrcPage->SectBitmap = pDstPage->SectBitmap = FULL_BITMAP_OF_SUPER_PAGE;
		pSrcPage->MDataPtr = pDstPage->MDataPtr = PHY_TMP_PAGE_CACHE;
		pSrcPage->SDataPtr = pDstPage->SDataPtr = PHY_TMP_SPARE_CACHE;
		ret = PHY_PageRead(pSrcPage);

		//add in 2010-06-18 by penggang, the logic layer needn't care the ecc error
		if(ret == -ERR_ECC)
			ret = 0;
		
		if (ret < 0){

			goto PHY_PageCopyback_exit;
		}
		ret = PHY_PageWrite(pDstPage);		
		goto PHY_PageCopyback_exit;
	}
	
	/*use copyback*/
	if (!SUPPORT_MULTI_PROGRAM)
	/*do not support two-plane copyback*/
		ret = _single_copy_back(pSrcPage,pDstPage);
	
	else
		ret = _multi_copy_back(pSrcPage,pDstPage);
PHY_PageCopyback_exit:	
	if (ret == -ERR_TIMEOUT)
		PHY_ERR("PHY_PageCopyback : copy back timeout\n");
	return ret;
}

__s32 PHY_ScanDDRParam(void)
{   
	__u32 i, j, k,chip = 0;
	__u32 good_ddr_param[64];
	__u32 good_ddr_param_cnt;
	__u8* main_buf = (__u8 *)MALLOC(8192);
	__u8  oob_buf[32];
	struct boot_physical_param readop;
	__s32 ret;
	__u32 sum, ddr_param;
	
	if(DDR_TYPE)
	{
	    for(k=0;k<NandStorageInfo.ChipCnt;k++)
	    {
	        chip = _cal_real_chip(k);
            readop.chip = chip;
            readop.block = 0;
            readop.page = 0;
            readop.sectorbitmap = 0x3;
            readop.mainbuf = main_buf;
            readop.oobbuf = oob_buf;
            
            for(i=1;i<16;i++) //delay_mode
            {
                
                good_ddr_param_cnt = 0;
                for(j=0;j<64;j++)
                {
                    PHY_DBG("(%d, %d) ", i, j);
                    NFC_InitDDRParam(chip, ((i<<8)|j));
                    ret = PHY_SimpleRead_1K(&readop);
                    
                    if(!ret) //find good ddr param
                    {
                        good_ddr_param[good_ddr_param_cnt] = ((i<<8)|j);
                        good_ddr_param_cnt++;
                        PHY_DBG(" ok\n");
                    }  
                    else
                    {
                        PHY_DBG(" fail\n");
                    }
                        
                }
                
                if(good_ddr_param_cnt)
                    break;
            }
            
            if(good_ddr_param_cnt)
            {
                sum = 0;
                for(i=0;i<good_ddr_param_cnt;i++)
                    sum += good_ddr_param[i];
                    
                ddr_param = sum/good_ddr_param_cnt;
                NFC_InitDDRParam(chip, ddr_param);
                
                PHY_DBG("PHY_ScanDDRParam, find a good ddr_param 0x%x in chip %d\n", ddr_param, chip);
            }
            else
            {
                PHY_ERR("PHY_ScanDDRParam, can't find a good ddr_param in chip %d\n", chip);
                FREE(main_buf, 8192);
                return -1;            
            }
	    }
        
    }
    
    FREE(main_buf, 8192);
    return 0;
}
