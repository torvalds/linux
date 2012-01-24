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

#include <linux/io.h>

struct __NandStorageInfo_t  NandStorageInfo;
struct __NandPageCachePool_t PageCachePool;
void __iomem *nand_base;

/**************************************************************************
************************* add one cmd to cmd list******************************
****************************************************************************/
void _add_cmd_list(NFC_CMD_LIST *cmd,__u32 value,__u32 addr_cycle,__u8 *addr,__u8 data_fetch_flag,
					__u8 main_data_fetch,__u32 bytecnt,__u8 wait_rb_flag)
{
	cmd->addr = addr;
	cmd->addr_cycle = addr_cycle;
	cmd->data_fetch_flag = data_fetch_flag;
	cmd->main_data_fetch = main_data_fetch;
	cmd->bytecnt = bytecnt;
	cmd->value = value;
	cmd->wait_rb_flag = wait_rb_flag;
	cmd->next = NULL;
}

/****************************************************************************
*********************translate (block + page+ sector) into 5 bytes addr***************
*****************************************************************************/
void _cal_addr_in_chip(__u32 block, __u32 page, __u32 sector,__u8 *addr, __u8 cycle)
{
	__u32 row;
	__u32 column;

	column = 512 * sector;
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



#if 0
__u8 _cal_real_chip(__u32 global_bank)
{
	__u8 chip;
	__u8 i,cnt;

	cnt = 0;
	chip = global_bank / BNK_CNT_OF_CHIP;

	for (i = 0; i <MAX_CHIP_SELECT_CNT; i++ ){
		if (CHIP_CONNECT_INFO & (1 << i)) {
			cnt++;
			if (cnt == (chip+1)){
				chip = i;
				return chip;
			}
		}
	}

	PHY_ERR("wrong chip number ,chip = %d, chip info = %x\n",chip,CHIP_CONNECT_INFO);

	return 0xff;
}
#endif

__u8 _cal_real_chip(__u32 global_bank)
{
	__u8 chip = 0;

	if((RB_CONNECT_MODE == 0)&&(global_bank<=2))
	{
	    if(global_bank)
		chip = 7;
	    else
		chip = 0;

	    return chip;
	}
	if((RB_CONNECT_MODE == 1)&&(global_bank<=1))
      	{
      	    chip = global_bank;
	    return chip;
      	}
	if((RB_CONNECT_MODE == 2)&&(global_bank<=2))
      	{
      	    chip = global_bank;
	    return chip;
      	}
	if((RB_CONNECT_MODE == 3)&&(global_bank<=2))
      	{
      	    chip = global_bank*2;
	    return chip;
      	}
	if((RB_CONNECT_MODE == 4)&&(global_bank<=4))
      	{
      	    switch(global_bank){
		  case 0:
		  	chip = 0;
			break;
		  case 1:
		  	chip = 2;
			break;
		  case 2:
		  	chip = 1;
			break;
		  case 3:
		  	chip = 3;
			break;
		  default :
		  	chip =0;
	    }

	    return chip;
      	}
	if((RB_CONNECT_MODE == 5)&&(global_bank<=4))
      	{
      	    chip = global_bank*2;

	    return chip;
      	}
	if((RB_CONNECT_MODE == 8)&&(global_bank<=8))
      	{
      	    switch(global_bank){
		  case 0:
		  	chip = 0;
			break;
		  case 1:
		  	chip = 2;
			break;
		  case 2:
		  	chip = 1;
			break;
		  case 3:
		  	chip = 3;
			break;
		  case 4:
		  	chip = 4;
			break;
		  case 5:
		  	chip = 6;
			break;
		  case 6:
		  	chip = 5;
			break;
		  case 7:
		  	chip = 7;
			break;
		  default : chip =0;

	    }

	    return chip;
      	}

	//dump_stack();
	PHY_ERR("wrong chip number ,rb_mode = %d, bank = %d, chip = %d, chip info = %x\n",RB_CONNECT_MODE, global_bank, chip, CHIP_CONNECT_INFO);

	return 0xff;
}

__u8 _cal_real_rb(__u32 chip)
{
	__u8 rb;


	rb = 0;

	if(RB_CONNECT_MODE == 0)
      	{
      	    rb = 0;
      	}
      if(RB_CONNECT_MODE == 1)
      	{
      	    rb = chip;
      	}
	if(RB_CONNECT_MODE == 2)
      	{
      	    rb = chip;
      	}
	if(RB_CONNECT_MODE == 3)
      	{
      	    rb = chip/2;
      	}
	if(RB_CONNECT_MODE == 4)
      	{
      	    rb = chip/2;
      	}
	if(RB_CONNECT_MODE == 5)
      	{
      	    rb = (chip/2)%2;
      	}
	if(RB_CONNECT_MODE == 8)
      	{
      	    rb = (chip/2)%2;
      	}

	if((rb!=0)&&(rb!=1))
	{
	    PHY_ERR("wrong Rb connect Mode  ,chip = %d, RbConnectMode = %d \n",chip,RB_CONNECT_MODE);
	    return 0xff;
	}

	return rb;
}

/*******************************************************************
**********************get status**************************************
********************************************************************/
__s32 _read_status(__u32 cmd_value, __u32 nBank)
{
	/*get status*/
	__u8 addr[5];
	__u32 addr_cycle;
	NFC_CMD_LIST cmd_list;

	addr_cycle = 0;

	if(!(cmd_value == 0x70 || cmd_value == 0x71))
	{
        	/* not 0x70 or 0x71, need send some address cycle */
       	 if(cmd_value == 0x78)
	 		addr_cycle = 3;
       	 else
       		addr_cycle = 1;
      		 _cal_addr_in_chip(nBank*BLOCK_CNT_OF_DIE,0,0,addr,addr_cycle);
	}
	_add_cmd_list(&cmd_list, cmd_value, addr_cycle, addr, 1,NFC_IGNORE,1,NFC_IGNORE);
	return (NFC_GetStatus(&cmd_list));

}

/********************************************************************
***************************wait rb ready*******************************
*********************************************************************/
__s32 _wait_rb_ready(__u32 chip)
{
	__s32 timeout = 0xffff;
	__u32 rb;


      rb = _cal_real_rb(chip);


	/*wait rb ready*/
	while((timeout--) && (NFC_CheckRbReady(rb)));
	if (timeout < 0)
		return -ERR_TIMEOUT;

	return 0;
}

void _pending_dma_irq_sem(void)
{
	return;
}

__s32 _read_single_page(struct boot_physical_param *readop,__u8 dma_wait_mode)
{
	__s32 ret;
	__u32 rb;

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


	ret = NFC_Read(cmd_list, readop->mainbuf, sparebuf, dma_wait_mode , NFC_PAGE_MODE);
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
*                       INIT NAND FLASH DRIVER PHYSICAL MODULE
*
* Description: init nand flash driver physical module.
*
* Aguments   : none
*
* Returns    : the resutl of initial.
*                   = 0     initial successful;
*                   = -1    initial failed.
************************************************************************************************************************
*/
__s32 PHY_Init(void)
{
	NFC_INIT_INFO nand_info;
	nand_info.bus_width = 0x0;
	nand_info.ce_ctl = 0x0;
	nand_info.ce_ctl1 = 0x0;
	nand_info.debug = 0x0;
	nand_info.pagesize = 4;
	nand_info.rb_sel = 1;
	nand_info.serial_access_mode = 1;

	//nand_base = ioremap(0x01C03000, 4096 );
	//modify by penggang for f20 linux

//	if (nand_base == NULL) {
//		printk(KERN_ERR "dma failed to remap register block\n");
//		return -ENOMEM;
//	}

	return (NFC_Init(&nand_info));
}

__s32 PHY_ChangeMode(__u8 serial_mode)
{

	NFC_INIT_INFO nand_info;

	/*memory allocate*/
	if (!PageCachePool.PageCache0){
		PageCachePool.PageCache0 = (__u8 *)MALLOC(SECTOR_CNT_OF_SUPER_PAGE * 512);
		if (!PageCachePool.PageCache0)
			return -1;
	}

	if (!PageCachePool.SpareCache){
		PageCachePool.SpareCache = (__u8 *)MALLOC(SECTOR_CNT_OF_SUPER_PAGE * 4);
		if (!PageCachePool.SpareCache)
			return -1;
	}

	if (!PageCachePool.TmpPageCache){
		PageCachePool.TmpPageCache = (__u8 *)MALLOC(SECTOR_CNT_OF_SUPER_PAGE * 512);
		if (!PageCachePool.TmpPageCache)
			return -1;
	}

      NFC_SetEccMode(ECC_MODE);


	nand_info.bus_width = 0x0;
	nand_info.ce_ctl = 0x0;
	nand_info.ce_ctl1 = 0x0;
	nand_info.debug = 0x0;
	nand_info.pagesize = SECTOR_CNT_OF_SINGLE_PAGE;


	nand_info.serial_access_mode = serial_mode;
	return (NFC_ChangMode(&nand_info));
}


/*
************************************************************************************************************************
*                       NAND FLASH DRIVER PHYSICAL MODULE EXIT
*
* Description: nand flash driver physical module exit.
*
* Aguments   : none
*
* Returns    : the resutl of exit.
*                   = 0     exit successful;
*                   = -1    exit failed.
************************************************************************************************************************
*/
__s32 PHY_Exit(void)
{

	if (PageCachePool.PageCache0){
		FREE(PageCachePool.PageCache0,SECTOR_CNT_OF_SUPER_PAGE * 512);
		PageCachePool.PageCache0 = NULL;
	}
	if (PageCachePool.SpareCache){
		FREE(PageCachePool.SpareCache,SECTOR_CNT_OF_SUPER_PAGE * 4);
		PageCachePool.SpareCache = NULL;
	}
	if (PageCachePool.TmpPageCache){
		FREE(PageCachePool.TmpPageCache,SECTOR_CNT_OF_SUPER_PAGE * 512);
		PageCachePool.TmpPageCache = NULL;
	}

	NFC_Exit();
	return 0;
}


/*
************************************************************************************************************************
*                       RESET ONE NAND FLASH CHIP
*
*Description: Reset the given nand chip;
*
*Arguments  : nChip     the chip select number, which need be reset.
*
*Return     : the result of chip reset;
*               = 0     reset nand chip successful;
*               = -1    reset nand chip failed.
************************************************************************************************************************
*/
__s32  PHY_ResetChip(__u32 nChip)
{
	__s32 ret;
	__s32 timeout = 0xffff;


	NFC_CMD_LIST cmd;

	NFC_SelectChip(nChip);

	_add_cmd_list(&cmd, 0xff, 0 , NFC_IGNORE, NFC_NO_DATA_FETCH, NFC_IGNORE, NFC_IGNORE, NFC_NO_WAIT_RB);
	ret = NFC_ResetChip(&cmd);

      	/*wait rb0 ready*/
	NFC_SelectRb(0);
	while((timeout--) && (NFC_CheckRbReady(0)));
	if (timeout < 0)
		return -ERR_TIMEOUT;

      /*wait rb0 ready*/
	NFC_SelectRb(1);
	while((timeout--) && (NFC_CheckRbReady(1)));
	if (timeout < 0)
		return -ERR_TIMEOUT;

	NFC_DeSelectChip(nChip);



	return ret;
}


/*
************************************************************************************************************************
*                       READ NAND FLASH ID
*
*Description: Read nand flash ID from the given nand chip.
*
*Arguments  : nChip         the chip number whoes ID need be read;
*             pChipID       the po__s32er to the chip ID buffer.
*
*Return     : read nand chip ID result;
*               = 0     read chip ID successful, the chip ID has been stored in given buffer;
*               = -1    read chip ID failed.
************************************************************************************************************************
*/

__s32  PHY_ReadNandId(__s32 nChip, void *pChipID)
{
	__s32 ret;


	NFC_CMD_LIST cmd;
	__u8 addr = 0;

	NFC_SelectChip(nChip);

	_add_cmd_list(&cmd, 0x90,1 , &addr, NFC_DATA_FETCH, NFC_IGNORE, 5, NFC_NO_WAIT_RB);
	ret = NFC_GetId(&cmd, pChipID);

	NFC_DeSelectChip(nChip);


	return ret;
}

/*
************************************************************************************************************************
*                       CHECK WRITE PROTECT STATUS
*
*Description: check the status of write protect.
*
*Arguments  : nChip     the number of chip, which nand chip need be checked.
*
*Return     : the result of status check;
*             = 0       the nand flash is not write proteced;
*             = 1       the nand flash is write proteced;
*             = -1      check status failed.
************************************************************************************************************************
*/
__s32  PHY_CheckWp(__u32 nChip)
{
	__s32 status;
	__u32 rb;


	rb = _cal_real_rb(nChip);
	NFC_SelectChip(nChip);
	NFC_SelectRb(rb);


	status = _read_status(0x70,nChip);
	NFC_DeSelectChip(nChip);
	NFC_DeSelectRb(rb);

	if (status < 0)
		return status;

	if (status & NAND_WRITE_PROTECT){
		return 1;
	}
	else
		return 0;
}

void _pending_rb_irq_sem(void)
{
	return;
}

void _do_irq(void)
{

}


__s32 PHY_SimpleRead (struct boot_physical_param *readop)
{
	//if (_read_single_page(readop,0) < 0)
	//	return FAIL;
	//return SUCESS;
	return (_read_single_page(readop,0));
}

/*
************************************************************************************************************************
*                       SYNC NAND FLASH PHYSIC OPERATION
*
*Description: Sync nand flash operation, check nand flash program/erase operation status.
*
*Arguments  : nBank     the number of the bank which need be synchronized;
*             bMode     the type of synch,
*                       = 0     sync the chip which the bank belonged to, wait the whole chip
*                               to be ready, and report status. if the chip support cacheprogram,
*                               need check if the chip is true ready;
*                       = 1     only sync the the bank, wait the bank ready and report the status,
*                               if the chip support cache program, need not check if the cache is
*                               true ready.
*
*Return     : the result of synch;
*               = 0     synch nand flash successful, nand operation ok;
*               = -1    synch nand flash failed.
************************************************************************************************************************
*/
__s32 PHY_SynchBank(__u32 nBank, __u32 bMode)
{
	__s32 ret,status;
	__u32 chip;
	__u32 rb;
	__u32 cmd_value;
	__s32 timeout = 0xffff;

	ret = 0;
	/*get chip no*/
	chip = _cal_real_chip(nBank);
	rb = _cal_real_rb(chip);

	if (0xff == chip){
		PHY_ERR("PHY_SynchBank : beyond chip count\n");
		return -ERR_INVALIDPHYADDR;
	}

	if ( (bMode == 1) && SUPPORT_INT_INTERLEAVE){
		if (nBank%BNK_CNT_OF_CHIP == 0)
			cmd_value = NandStorageInfo.OptPhyOpPar.InterBnk0StatusCmd;
		else
			cmd_value = NandStorageInfo.OptPhyOpPar.InterBnk1StatusCmd;
	}
	else{
		if (SUPPORT_MULTI_PROGRAM)
			cmd_value = NandStorageInfo.OptPhyOpPar.MultiPlaneStatusCmd;
		else
			cmd_value = 0x70;
	}

	/*if support rb irq , last op is erase or write*/
	if (SUPPORT_RB_IRQ)
		_pending_rb_irq_sem();

	NFC_SelectChip(chip);
	NFC_SelectRb(rb);

	while(1){
		status = _read_status(cmd_value,nBank%BNK_CNT_OF_CHIP);
		if (status < 0)
			return status;

		if (status & NAND_STATUS_READY)
			break;

		if (timeout-- < 0){
			PHY_ERR("PHY_SynchBank : wait nand ready timeout,chip = %x, bank = %x, cmd value = %x, status = %x\n",chip,nBank,cmd_value,status);
			return -ERR_TIMEOUT;
		}
	}
	 if(status & NAND_OPERATE_FAIL)
	 	ret = NAND_OP_FALSE;
	NFC_DeSelectChip(chip);
	NFC_DeSelectRb(rb);


	return ret;
}
