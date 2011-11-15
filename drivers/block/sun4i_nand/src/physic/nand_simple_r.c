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
#include "../../nfc/nfc_i.h"

#include <linux/io.h>

struct __NandStorageInfo_t  NandStorageInfo;
struct __NandPageCachePool_t PageCachePool;
__u32 RetryCount[8];
__u16 random_seed[128];
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

__s32 _wait_rb_ready_int(__u32 chip)
{
	__u32 rb;
	
    rb = _cal_real_rb(chip);
    NFC_SelectRb(rb);
  
    if(NFC_CheckRbReady(rb))
    {
       NAND_WaitRbReady();
    }
    else
    {
        //printk("fast rb ready \n");
    }
    
    while(NFC_CheckRbReady(rb))
    {
        printk("rb int error!\n");
    }
	
	
	return 0;
}



void _pending_dma_irq_sem(void)
{
	return;
}

void _random_seed_init(void)
{
	random_seed[0] = 0x2b75; 
    random_seed[1] = 0x0bd0; 
    random_seed[2] = 0x5ca3; 
    random_seed[3] = 0x62d1; 
    random_seed[4] = 0x1c93; 
    random_seed[5] = 0x07e9; 
    random_seed[6] = 0x2162; 
    random_seed[7] = 0x3a72; 
    random_seed[8] = 0x0d67; 
    random_seed[9] = 0x67f9; 
    random_seed[10] = 0x1be7; 
    random_seed[11] = 0x077d; 
    random_seed[12] = 0x032f; 
    random_seed[13] = 0x0dac; 
    random_seed[14] = 0x2716; 
    random_seed[15] = 0x2436; 
    random_seed[16] = 0x7922; 
    random_seed[17] = 0x1510; 
    random_seed[18] = 0x3860; 
    random_seed[19] = 0x5287; 
    random_seed[20] = 0x480f; 
    random_seed[21] = 0x4252; 
    random_seed[22] = 0x1789; 
    random_seed[23] = 0x5a2d; 
    random_seed[24] = 0x2a49; 
    random_seed[25] = 0x5e10; 
    random_seed[26] = 0x437f; 
    random_seed[27] = 0x4b4e; 
    random_seed[28] = 0x2f45; 
    random_seed[29] = 0x216e; 
    random_seed[30] = 0x5cb7; 
    random_seed[31] = 0x7130; 
    random_seed[32] = 0x2a3f; 
    random_seed[33] = 0x60e4; 
    random_seed[34] = 0x4dc9; 
    random_seed[35] = 0x0ef0; 
    random_seed[36] = 0x0f52; 
    random_seed[37] = 0x1bb9; 
    random_seed[38] = 0x6211; 
    random_seed[39] = 0x7a56; 
    random_seed[40] = 0x226d; 
    random_seed[41] = 0x4ea7; 
    random_seed[42] = 0x6f36; 
    random_seed[43] = 0x3692; 
    random_seed[44] = 0x38bf; 
    random_seed[45] = 0x0c62; 
    random_seed[46] = 0x05eb; 
    random_seed[47] = 0x4c55; 
    random_seed[48] = 0x60f4; 
    random_seed[49] = 0x728c; 
    random_seed[50] = 0x3b6f; 
    random_seed[51] = 0x2037; 
    random_seed[52] = 0x7f69; 
    random_seed[53] = 0x0936; 
    random_seed[54] = 0x651a; 
    random_seed[55] = 0x4ceb; 
    random_seed[56] = 0x6218; 
    random_seed[57] = 0x79f3; 
    random_seed[58] = 0x383f; 
    random_seed[59] = 0x18d9; 
    random_seed[60] = 0x4f05; 
    random_seed[61] = 0x5c82; 
    random_seed[62] = 0x2912; 
    random_seed[63] = 0x6f17; 
    random_seed[64] = 0x6856; 
    random_seed[65] = 0x5938; 
    random_seed[66] = 0x1007; 
    random_seed[67] = 0x61ab; 
    random_seed[68] = 0x3e7f; 
    random_seed[69] = 0x57c2; 
    random_seed[70] = 0x542f; 
    random_seed[71] = 0x4f62; 
    random_seed[72] = 0x7454; 
    random_seed[73] = 0x2eac; 
    random_seed[74] = 0x7739; 
    random_seed[75] = 0x42d4; 
    random_seed[76] = 0x2f90; 
    random_seed[77] = 0x435a; 
    random_seed[78] = 0x2e52; 
    random_seed[79] = 0x2064; 
    random_seed[80] = 0x637c; 
    random_seed[81] = 0x66ad; 
    random_seed[82] = 0x2c90; 
    random_seed[83] = 0x0bad; 
    random_seed[84] = 0x759c; 
    random_seed[85] = 0x0029; 
    random_seed[86] = 0x0986; 
    random_seed[87] = 0x7126; 
    random_seed[88] = 0x1ca7; 
    random_seed[89] = 0x1605; 
    random_seed[90] = 0x386a; 
    random_seed[91] = 0x27f5; 
    random_seed[92] = 0x1380; 
    random_seed[93] = 0x6d75; 
    random_seed[94] = 0x24c3; 
    random_seed[95] = 0x0f8e; 
    random_seed[96] = 0x2b7a; 
    random_seed[97] = 0x1418; 
    random_seed[98] = 0x1fd1; 
    random_seed[99] = 0x7dc1; 
    random_seed[100] = 0x2d8e; 
    random_seed[101] = 0x43af; 
    random_seed[102] = 0x2267; 
    random_seed[103] = 0x7da3; 
    random_seed[104] = 0x4e3d; 
    random_seed[105] = 0x1338; 
    random_seed[106] = 0x50db; 
    random_seed[107] = 0x454d; 
    random_seed[108] = 0x764d; 
    random_seed[109] = 0x40a3; 
    random_seed[110] = 0x42e6; 
    random_seed[111] = 0x262b; 
    random_seed[112] = 0x2d2e; 
    random_seed[113] = 0x1aea; 
    random_seed[114] = 0x2e17; 
    random_seed[115] = 0x173d; 
    random_seed[116] = 0x3a6e; 
    random_seed[117] = 0x71bf; 
    random_seed[118] = 0x25f9; 
    random_seed[119] = 0x0a5d; 
    random_seed[120] = 0x7c57; 
    random_seed[121] = 0x0fbe; 
    random_seed[122] = 0x46ce; 
    random_seed[123] = 0x4939; 
    random_seed[124] = 0x6b17; 
    random_seed[125] = 0x37bb; 
    random_seed[126] = 0x3e91; 
    random_seed[127] = 0x76db; 

}

__u32 _cal_random_seed(__u32 page)
{
	__u32 randomseed;

	randomseed = random_seed[page%128];
	
	return randomseed;
}

__s32 _read_single_page(struct boot_physical_param *readop,__u8 dma_wait_mode)
{
	__s32 ret;
	__u32 k = 0;
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

    if(SUPPORT_READ_RETRY)  
    {
        for( k = 0; k<READ_RETRY_CYCLE+1;k++)
		{
			if(RetryCount[readop->chip]==(READ_RETRY_CYCLE+1))
				RetryCount[readop->chip] = 0;

			if(k>0)
			{
			    if(NFC_ReadRetry(readop->chip,RetryCount[readop->chip],READ_RETRY_TYPE))
			    {
			        PHY_ERR("[Read_single_page] NFC_ReadRetry fail \n");
			        return -1;
			    }
			}

			if(SUPPORT_RANDOM)
			{
				random_seed = _cal_random_seed(readop->page);
				NFC_SetRandomSeed(random_seed);
				NFC_RandomEnable();
				ret = NFC_Read(cmd_list, readop->mainbuf, sparebuf, dma_wait_mode , NFC_PAGE_MODE);
				NFC_RandomDisable();
				if(ret == -ERR_ECC)
					ret = NFC_Read(cmd_list, readop->mainbuf, sparebuf, dma_wait_mode , NFC_PAGE_MODE);
			}
			else
			{
				ret = NFC_Read(cmd_list, readop->mainbuf, sparebuf, dma_wait_mode , NFC_PAGE_MODE);
			}

			if((ret != -ERR_ECC)||(k==READ_RETRY_CYCLE))
			{
				break;
			}

			RetryCount[readop->chip]++;    				    				
		}

	if(k>0)
		PHY_DBG("[Read_single_page] NFC_ReadRetry %d cycles, chip = %d,RetryCount = %d  \n", k ,readop->chip,RetryCount[readop->chip]);
	
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
			ret = NFC_Read(cmd_list, readop->mainbuf, sparebuf, dma_wait_mode , NFC_PAGE_MODE);
			NFC_RandomDisable();
			if(ret == -ERR_ECC)
				ret = NFC_Read(cmd_list, readop->mainbuf, sparebuf, dma_wait_mode , NFC_PAGE_MODE);
		}
		else
		{
			ret = NFC_Read(cmd_list, readop->mainbuf, sparebuf, dma_wait_mode , NFC_PAGE_MODE);
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
    __s32 ret;
    __u32 i;
	NFC_INIT_INFO nand_info;		   
    //init RetryCount
    for(i=0; i<8; i++)
        RetryCount[i] = 0;
	nand_info.bus_width = 0x0;
	nand_info.ce_ctl = 0x0;
	nand_info.ce_ctl1 = 0x0;
	nand_info.debug = 0x0;
	nand_info.pagesize = 4;
	nand_info.rb_sel = 1;
	nand_info.serial_access_mode = 1;
	ret = NFC_Init(&nand_info);
		
    PHY_DBG("NFC Randomizer start. \n");
	_random_seed_init();
	NFC_RandomDisable();
	
	return ret;	
}

__s32 PHY_GetDefaultParam(__u32 bank)
{   
	__u32 chip = 0;
	__u8 default_value[16];
	
    chip = _cal_real_chip(bank);
    NFC_SelectChip(chip);
    NFC_GetDefaultParam(chip, default_value, READ_RETRY_TYPE);
	NFC_SetDefaultParam(chip, default_value, READ_RETRY_TYPE);
    PHY_DBG("PHY_GetDefaultParam: chip 0x%x, Read Retry Default Value is 0x%x, 0x%x, 0x%x, 0x%x \n", chip, default_value[0], default_value[1], default_value[2],default_value[3]);

    
    return 0;
}

__s32 PHY_SetDefaultParam(__u32 bank)
{   
	__u32 chip = 0;
	__u8 default_value[16];
	__u8 temp_value[16];
	
	chip = _cal_real_chip(bank);
    NFC_SelectChip(chip);
    NFC_SetDefaultParam(chip, default_value, READ_RETRY_TYPE);
	PHY_DBG("PHY_SetDefaultParam: Read Retry Type is : 0x%x \n", READ_RETRY_TYPE);
    PHY_DBG("PHY_SetDefaultParam: chip 0x%x, Read Retry Default Value is 0x%x, 0x%x, 0x%x, 0x%x \n", chip, default_value[0], default_value[1], default_value[2],default_value[3]);
	NFC_GetDefaultParam(chip, temp_value, READ_RETRY_TYPE);
    PHY_DBG("PHY_SetDefaultParam: chip 0x%x, Read Default Value After Set value is 0x%x, 0x%x, 0x%x, 0x%x \n", chip, temp_value[0], temp_value[1], temp_value[2],temp_value[3]);
    
    return 0;
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
    __u32 i = 0;

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
	
	if(SUPPORT_READ_RETRY)
	{
	    for(i=0; i<NandStorageInfo.ChipCnt;i++)
        {
            PHY_SetDefaultParam(i);
        }
        NFC_ReadRetryExit(READ_RETRY_TYPE);
	}
	
	
	NFC_RandomDisable();

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

__s32  PHY_ReadNandUniqueId(__s32 bank, void *pChipID)
{
	__s32 ret, err_flag;
	__u32 i, j,nChip, nRb;
	__u8 *temp_id;


	NFC_CMD_LIST cmd;
	__u8 addr = 0;

	nChip = _cal_real_chip(bank);
	NFC_SelectChip(nChip);
	nRb = _cal_real_rb(nChip);
	NFC_SelectRb(nRb);

	for(i=0;i<16; i++)
	{
		addr = i*32;
		temp_id = (__u8*)(pChipID);
		err_flag = 0;
		
		_add_cmd_list(&cmd, 0xed,1 , &addr, NFC_DATA_FETCH, NFC_IGNORE, 32, NFC_WAIT_RB);
		ret = NFC_GetUniqueId(&cmd, pChipID);

		for(j=0; j<16;j++)
		{
			if((temp_id[j]^temp_id[j+16]) != 0xff)
			{
				err_flag = 1;
				ret = -1;
				break;
			}
		}

		if(err_flag == 0)
		{
			ret = 0;		
			break;
		}
		
	}

	if(ret)
	{
		for(j=0; j<32;j++)
		{
			temp_id[j] = 0x55;
		}
	}

	PHY_DBG("Nand Unique ID of chip %u is : \n", nChip);
	PHY_DBG("%x, %x, %x, %x\n", temp_id[0],temp_id[1],temp_id[2],temp_id[3]);
	PHY_DBG("%x, %x, %x, %x\n", temp_id[4],temp_id[5],temp_id[6],temp_id[7]);
	PHY_DBG("%x, %x, %x, %x\n", temp_id[8],temp_id[9],temp_id[10],temp_id[11]);
	PHY_DBG("%x, %x, %x, %x\n", temp_id[12],temp_id[13],temp_id[14],temp_id[15]);
	PHY_DBG("\n");
	PHY_DBG("%x, %x, %x, %x\n", temp_id[16],temp_id[17],temp_id[18],temp_id[19]);
	PHY_DBG("%x, %x, %x, %x\n", temp_id[20],temp_id[21],temp_id[22],temp_id[23]);
	PHY_DBG("%x, %x, %x, %x\n", temp_id[24],temp_id[25],temp_id[26],temp_id[27]);
	PHY_DBG("%x, %x, %x, %x\n", temp_id[28],temp_id[29],temp_id[30],temp_id[31]);
	PHY_DBG("\n");
	
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
		
	_wait_rb_ready_int(chip);
	
	NFC_SelectChip(chip);
	NFC_SelectRb(rb);

	while(1){
		status = _read_status(cmd_value,nBank%BNK_CNT_OF_CHIP);
		if (status < 0)
		{
		    PHY_ERR("PHY_SynchBank : read status invalid ,chip = %x, bank = %x, cmd value = %x, status = %x\n",chip,nBank,cmd_value,status);
		    return status;
		}
		if (status & NAND_STATUS_READY)
			break;
		
		if (timeout-- < 0){
			PHY_ERR("PHY_SynchBank : wait nand ready timeout,chip = %x, bank = %x, cmd value = %x, status = %x\n",chip,nBank,cmd_value,status);
			return -ERR_TIMEOUT;
		}
	}
	 if(status & NAND_OPERATE_FAIL)
	{
	    PHY_ERR("PHY_SynchBank : last W/E operation fail,chip = %x, bank = %x, cmd value = %x, status = %x\n",chip,nBank,cmd_value,status);
	    ret = NAND_OP_FALSE;
	}
	NFC_DeSelectChip(chip);
	NFC_DeSelectRb(rb);

	
	return ret;
}
