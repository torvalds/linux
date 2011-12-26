/*********************************************************************************
*                                           NAND FLASH DRIVER
*								(c) Copyright 2008, SoftWinners Co,Ld.
*                                          All Right Reserved
*file : nfc_w.c
*description : this file provides some physic functions for upper nand driver layer.
*history :
*	v0.1  2008-03-26 Richard
*	        offer direct accsee method to nand flash control machine.
*   v0.2  2009.09.09 penggang
**********************************************************************************/
#include "nfc_i.h"

extern __u32 pagesize;
extern __s32 _wait_cmdfifo_free(void);
extern __s32 _wait_cmd_finish(void);
extern void _dma_config_start(__u8 rw, __u32 buff_addr, __u32 len);
extern __s32 _wait_dma_end(void);
extern __s32 _check_ecc(__u32 eblock_cnt);
extern void _disable_ecc(void);
extern void _enable_ecc(__u32 pipline);
extern __s32 _enter_nand_critical(void);
extern __s32 _exit_nand_critical(void);
extern void _set_addr(__u8 *addr, __u8 cnt);
extern __s32 nfc_set_cmd_register(NFC_CMD_LIST *cmd);
extern __s32 _read_in_page_mode(NFC_CMD_LIST  *rcmd,void *mainbuf,void *sparebuf,__u8 dma_wait_mode);
extern __s32 _read_in_normal_mode(NFC_CMD_LIST  *rcmd, __u8 *mainbuf, __u8 *sparebuf,__u8 dma_wait_mode);


extern __u8 read_retry_reg_adr[READ_RETRY_MAX_REG_NUM];
extern __u8 read_retry_default_val[8][READ_RETRY_MAX_REG_NUM];
extern __s16 read_retry_val[READ_RETRY_MAX_CYCLE][READ_RETRY_MAX_REG_NUM];
extern __u8 read_retry_mode;
extern __u8 read_retry_cycle;
extern __u8 read_retry_reg_num;

extern __u8 lsb_mode_reg_adr[LSB_MODE_MAX_REG_NUM];
extern __u8 lsb_mode_default_val[LSB_MODE_MAX_REG_NUM];
extern __u8 lsb_mode_val[LSB_MODE_MAX_REG_NUM];
extern __u8 lsb_mode_reg_num;

extern __s32 _vender_get_param(__u8 *para, __u8 *addr, __u32 count);
extern __s32 _vender_set_param(__u8 *para, __u8 *addr, __u32 count);
extern __s32 _vender_pre_condition(void);


/*after send write or erase command, must wait rb from ready to busy, then can send status command
	because nfc not do this, so software delay by xr, 2009-3-25*/

void _wait_twb(void)
{
/*
	__u32 timeout = 800;

	while ( (timeout--) && !(NFC_READ_REG(NFC_REG_ST) & NFC_CMD_FIFO_STATUS));
*/
}

/*******************************************************************************
*								NFC_Write
*
* Description 	: write one page data into flash in single plane mode.
* Arguments	: *wcmd	-- the write command sequence list head¡£
*			  *mainbuf	-- point to data buffer address, 	it must be four bytes align.
*                     *sparebuf	-- point to spare buffer address.
*                     dma_wait_mode	-- how to deal when dma start, 0 = wait till dma finish,
							    1 = dma interrupt was set and now sleep till interrupt occurs.
*			  rb_wait_mode -- 0 = do not care rb, 1 = set rb interrupt and do not wait rb ready.
*			  page_mode  -- 0 = common command, 1 = page command.
* Returns		: 0 = success.
			  -1 = fail.
* Notes		: the unit must be page, so if  page_mode is not 1, return fail,the function exits without checking status,
			  if the commands do not fetch data,ecc is not neccesary.
********************************************************************************/
__s32 NFC_Write( NFC_CMD_LIST  *wcmd, void *mainbuf, void *sparebuf,  __u8 dma_wait_mode, __u8 rb_wait_mode,
				    __u8 page_mode)
{
	__s32 ret;
	__s32 i;
	__u32 cfg;
	__u32 program_cmd,random_program_cmd;
	NFC_CMD_LIST *cur_cmd,*program_addr_cmd;

	if (page_mode == 0){
		return -1;
	}

	ret = 0;
	_enter_nand_critical();

	/*write in page_mode*/
	program_addr_cmd = wcmd;
	cur_cmd = wcmd;
	cur_cmd = cur_cmd->next;
	random_program_cmd = cur_cmd->value;
	cur_cmd = cur_cmd->next;
	program_cmd = cur_cmd->value;

	//access NFC internal RAM by DMA bus
	NFC_WRITE_REG(NFC_REG_CTL, (NFC_READ_REG(NFC_REG_CTL)) | NFC_RAM_METHOD);

	/*set dma and run*/
//	if (NFC_IS_SDRAM((__u32)mainbuf))
//		attr = 0x2930281;
//	else
//		attr = 0x2930280;

	_dma_config_start(1, (__u32)mainbuf, pagesize);

	/*wait cmd fifo free*/
	ret = _wait_cmdfifo_free();
	if (ret){
		_exit_nand_critical();
		return ret;
	}
	/*set NFC_REG_CNT*/
  NFC_WRITE_REG(NFC_REG_CNT,1024);

	/*set NFC_REG_RCMD_SET*/
	cfg = 0;
	cfg |= (program_cmd & 0xff);
	cfg |= ((random_program_cmd & 0xff) << 8);
	NFC_WRITE_REG(NFC_REG_WCMD_SET, cfg);

	/*set NFC_REG_SECTOR_NUM*/
	NFC_WRITE_REG(NFC_REG_SECTOR_NUM, pagesize/1024);

	/*set user data*/
	for (i = 0; i < pagesize/1024;  i++){
		NFC_WRITE_REG(NFC_REG_USER_DATA(i), *((__u32 *)sparebuf + i) );
	}

	/*set addr*/
	_set_addr(program_addr_cmd->addr,program_addr_cmd->addr_cycle);

	/*set NFC_REG_CMD*/
	cfg  = 0;
	 /*set sequence mode*/
	//cfg |= 0x1<<25;
	cfg |= program_addr_cmd->value;
	cfg |= ( (program_addr_cmd->addr_cycle - 1) << 16);
	//cfg |= (NFC_SEND_ADR | NFC_ACCESS_DIR | NFC_DATA_TRANS | NFC_SEND_CMD | NFC_WAIT_FLAG | NFC_DATA_SWAP_METHOD);
	cfg |= (NFC_SEND_ADR | NFC_ACCESS_DIR | NFC_DATA_TRANS | NFC_SEND_CMD1 | NFC_SEND_CMD2 | NFC_DATA_SWAP_METHOD);
	cfg |= ((__u32)0x2 << 30);//page command
	if (pagesize/1024 == 1)
		cfg |= NFC_SEQ;

	/*enable ecc*/
	_enable_ecc(1);
	NFC_WRITE_REG(NFC_REG_CMD,cfg);

    NAND_WaitDmaFinish();

	_wait_twb();
	_wait_cmdfifo_free();
	_wait_cmd_finish();

	/*start dma?*/
	/*if dma mode is wait*/
	if(0 == dma_wait_mode){
		ret = _wait_dma_end();
	}

	/*disable ecc*/
	_disable_ecc();

	/*switch to ahb*/
	NFC_WRITE_REG(NFC_REG_CTL, (NFC_READ_REG(NFC_REG_CTL)) & (~NFC_RAM_METHOD));

	_exit_nand_critical();
	return ret;
}

__s32 NFC_Write_Seq( NFC_CMD_LIST  *wcmd, void *mainbuf, void *sparebuf,  __u8 dma_wait_mode, __u8 rb_wait_mode,
				    __u8 page_mode)
{
	__s32 ret;
	__s32 i;
	__u32 cfg;
	__u32 ecc_mode_temp;
	__u32 program_cmd,random_program_cmd;
	NFC_CMD_LIST *cur_cmd,*program_addr_cmd;

	if (page_mode == 0){
		return -1;
	}

	ret = 0;
	_enter_nand_critical();

	/*write in page_mode*/
	program_addr_cmd = wcmd;
	cur_cmd = wcmd;
	cur_cmd = cur_cmd->next;
	random_program_cmd = cur_cmd->value;
	cur_cmd = cur_cmd->next;
	program_cmd = cur_cmd->value;

	//access NFC internal RAM by DMA bus
	NFC_WRITE_REG(NFC_REG_CTL, (NFC_READ_REG(NFC_REG_CTL)) | NFC_RAM_METHOD);

//	/*set dma and run*/
//	if (NFC_IS_SDRAM((__u32)mainbuf))
//		attr = 0x2930281;
//	else
//		attr = 0x2930280;

	_dma_config_start(1, (__u32)mainbuf, pagesize);

	/*wait cmd fifo free*/
	ret = _wait_cmdfifo_free();
	if (ret){
		_exit_nand_critical();
		return ret;
	}
	/*set NFC_REG_CNT*/
  NFC_WRITE_REG(NFC_REG_CNT,1024);

	/*set NFC_REG_RCMD_SET*/
	cfg = 0;
	cfg |= (program_cmd & 0xff);
	cfg |= ((random_program_cmd & 0xff) << 8);
	NFC_WRITE_REG(NFC_REG_WCMD_SET, cfg);

	/*set NFC_REG_SECTOR_NUM*/
	NFC_WRITE_REG(NFC_REG_SECTOR_NUM, pagesize/1024);

	/*set user data*/
	for (i = 0; i < pagesize/1024;  i++){
		NFC_WRITE_REG(NFC_REG_USER_DATA(i), *((__u32 *)sparebuf + i) );
	}

	/*set addr*/
	_set_addr(program_addr_cmd->addr,program_addr_cmd->addr_cycle);

	/*set NFC_REG_CMD*/
	cfg  = 0;
	 /*set sequence mode*/
	cfg |= 0x1<<25;
	cfg |= program_addr_cmd->value;
	cfg |= ( (program_addr_cmd->addr_cycle - 1) << 16);
	//cfg |= (NFC_SEND_ADR | NFC_ACCESS_DIR | NFC_DATA_TRANS | NFC_SEND_CMD | NFC_WAIT_FLAG | NFC_DATA_SWAP_METHOD);
	cfg |= (NFC_SEND_ADR | NFC_ACCESS_DIR | NFC_DATA_TRANS | NFC_SEND_CMD1 | NFC_SEND_CMD2 | NFC_DATA_SWAP_METHOD);
	cfg |= ((__u32)0x2 << 30);//page command
	if (pagesize/1024 == 1)
		cfg |= NFC_SEQ;

	/*enable ecc*/
	_enable_ecc(1);
	
	/*set ecc to 24-bit ecc*/
    ecc_mode_temp = NFC_READ_REG(NFC_REG_ECC_CTL) & 0xf000;
	NFC_WRITE_REG(NFC_REG_ECC_CTL, ((NFC_READ_REG(NFC_REG_ECC_CTL) & (~NFC_ECC_MODE))|(0x1<<12) ));
	
	NFC_WRITE_REG(NFC_REG_CMD,cfg);

    NAND_WaitDmaFinish();

	_wait_twb();
	_wait_cmdfifo_free();
	_wait_cmd_finish();

	/*start dma?*/
	/*if dma mode is wait*/
	if(0 == dma_wait_mode){
		ret = _wait_dma_end();
	}

	/*disable ecc*/
	_disable_ecc();
	
	/*set ecc to original value*/
	NFC_WRITE_REG(NFC_REG_ECC_CTL, (NFC_READ_REG(NFC_REG_ECC_CTL) & (~NFC_ECC_MODE))|ecc_mode_temp);

	/*switch to ahb*/
	NFC_WRITE_REG(NFC_REG_CTL, (NFC_READ_REG(NFC_REG_CTL)) & (~NFC_RAM_METHOD));

	_exit_nand_critical();
	return ret;
}

__s32 NFC_Write_1K( NFC_CMD_LIST  *wcmd, void *mainbuf, void *sparebuf,  __u8 dma_wait_mode, __u8 rb_wait_mode,
				    __u8 page_mode)
{
	__s32 ret;
	__s32 i;
	__u32 cfg;
	__u32 page_size_temp, ecc_mode_temp;
	__u32 program_cmd,random_program_cmd;
	NFC_CMD_LIST *cur_cmd,*program_addr_cmd;

	if (page_mode == 0){
		return -1;
	}

	ret = 0;
	_enter_nand_critical();

	/*write in page_mode*/
	program_addr_cmd = wcmd;
	cur_cmd = wcmd;
	cur_cmd = cur_cmd->next;
	random_program_cmd = cur_cmd->value;
	cur_cmd = cur_cmd->next;
	program_cmd = cur_cmd->value;

	//access NFC internal RAM by DMA bus
	NFC_WRITE_REG(NFC_REG_CTL, (NFC_READ_REG(NFC_REG_CTL)) | NFC_RAM_METHOD);

	//set pagesize to 1K
      page_size_temp = (NFC_READ_REG(NFC_REG_CTL) & 0xf00)>>8;
	NFC_WRITE_REG(NFC_REG_CTL, (NFC_READ_REG(NFC_REG_CTL)) | (0x3<<8));

//	/*set dma and run*/
//	if (NFC_IS_SDRAM((__u32)mainbuf))
//		attr = 0x2930281;
//	else
//		attr = 0x2930280;

	_dma_config_start(1, (__u32)mainbuf, 1024);

	/*wait cmd fifo free*/
	ret = _wait_cmdfifo_free();
	if (ret){
		_exit_nand_critical();
		return ret;
	}
	/*set NFC_REG_CNT*/
  NFC_WRITE_REG(NFC_REG_CNT,1024);

	/*set NFC_REG_RCMD_SET*/
	cfg = 0;
	cfg |= (program_cmd & 0xff);
	cfg |= ((random_program_cmd & 0xff) << 8);
	NFC_WRITE_REG(NFC_REG_WCMD_SET, cfg);

	/*set NFC_REG_SECTOR_NUM*/
	NFC_WRITE_REG(NFC_REG_SECTOR_NUM, 1024/1024);

	/*set user data*/
	for (i = 0; i < 1024/1024;  i++){
		NFC_WRITE_REG(NFC_REG_USER_DATA(i), *((__u32 *)sparebuf + i) );
	}

	/*set addr*/
	_set_addr(program_addr_cmd->addr,program_addr_cmd->addr_cycle);

	/*set NFC_REG_CMD*/
	cfg  = 0;
	 /*set sequence mode*/
	cfg |= 0x1<<25;
	cfg |= program_addr_cmd->value;
	cfg |= ( (program_addr_cmd->addr_cycle - 1) << 16);
	//cfg |= (NFC_SEND_ADR | NFC_ACCESS_DIR | NFC_DATA_TRANS | NFC_SEND_CMD | NFC_WAIT_FLAG | NFC_DATA_SWAP_METHOD);
	cfg |= (NFC_SEND_ADR | NFC_ACCESS_DIR | NFC_DATA_TRANS | NFC_SEND_CMD1 | NFC_SEND_CMD2 | NFC_DATA_SWAP_METHOD);
	cfg |= ((__u32)0x2 << 30);//page command
	if (pagesize/1024 == 1)
		cfg |= NFC_SEQ;

	/*enable ecc*/
	_enable_ecc(1);

	/*set ecc to 64-bit ecc*/
    ecc_mode_temp = NFC_READ_REG(NFC_REG_ECC_CTL) & 0xf000;
	NFC_WRITE_REG(NFC_REG_ECC_CTL, ((NFC_READ_REG(NFC_REG_ECC_CTL) & (~NFC_ECC_MODE))|(0x8<<12) ));
	NFC_WRITE_REG(NFC_REG_CMD,cfg);

    NAND_WaitDmaFinish();

	_wait_twb();
	_wait_cmdfifo_free();
	_wait_cmd_finish();

	/*start dma?*/
	/*if dma mode is wait*/
	if(0 == dma_wait_mode){
		ret = _wait_dma_end();
	}



	/*disable ecc*/
	_disable_ecc();

	/*set ecc to original value*/
	NFC_WRITE_REG(NFC_REG_ECC_CTL, (NFC_READ_REG(NFC_REG_ECC_CTL) & (~NFC_ECC_MODE))|ecc_mode_temp);

      /*set pagesize to original value*/
      NFC_WRITE_REG(NFC_REG_CTL, ((NFC_READ_REG(NFC_REG_CTL)) & (~NFC_PAGE_SIZE)) | (page_size_temp<<8));

	/*switch to ahb*/
	NFC_WRITE_REG(NFC_REG_CTL, (NFC_READ_REG(NFC_REG_CTL)) & (~NFC_RAM_METHOD));

	_exit_nand_critical();
	return ret;
}

/*******************************************************************************
*								NFC_Erase
*
* Description 	: erase one block in signle plane mode or multi plane mode.
* Arguments	: *ecmd	-- the erase command sequence list head
*			  rb_wait_mode  -- 0 = do not care rb, 1 = set rb interrupt and do not wait rb ready.
* Returns		: 0 = success.
			  -1 = fail.
* Notes		: the unit must be page, so if  page_mode is not 1, return fail,the function exits without checking status,
			  if the commands do not fetch data,ecc is not neccesary.
********************************************************************************/
__s32 NFC_Erase(NFC_CMD_LIST  *ecmd, __u8 rb_wait_mode)
{

	__s32 ret;

	_enter_nand_critical();

	ret = nfc_set_cmd_register(ecmd);
	if (ret){
		_exit_nand_critical();
		return ret;
	}
	_wait_twb();
	ret = _wait_cmdfifo_free();
	ret |= _wait_cmd_finish();

	_exit_nand_critical();

	return ret;
}

/*******************************************************************************
*								NFC_CopyBackRead
*
* Description 	: copyback read one page data inside flash in single plane mode or multi plane mode
* Arguments	: *crcmd	-- the copyback read command sequence list head.
* Returns		: 0 = success.
			  -1 = fail.
* Notes		: the unit must be page.
********************************************************************************/
__s32 NFC_CopyBackRead(NFC_CMD_LIST  *crcmd)
{

	__s32 ret;

	_enter_nand_critical();
	ret = nfc_set_cmd_register(crcmd);
	if (ret){
		_exit_nand_critical();
		return ret;
	}

	ret = _wait_cmdfifo_free();
	ret |= _wait_cmd_finish();

	_exit_nand_critical();

	return ret;
}
//#pragma arm section code="NFC_CopyBackWrite"
/*******************************************************************************
*								NFC_CopyBackWrite
*
* Description 	: copyback write one page data inside flash in single plane mode or multi plane mode
* Arguments	: *cwcmd	-- the copyback read command sequence list head.
 			  rb_wait_mode  -- 0 = do not care rb, 1 = set rb interrupt and do not wait rb ready.
* Returns		: 0 = success.
			  -1 = fail.
* Notes		: the unit must be page.
********************************************************************************/
__s32 NFC_CopyBackWrite(NFC_CMD_LIST  *cwcmd, __u8 rb_wait_mode)
{
	__s32 ret;

	_enter_nand_critical();

	ret = nfc_set_cmd_register(cwcmd);
	if (ret){
		_exit_nand_critical();
		return ret;
	}
	_wait_twb();
	ret = _wait_cmdfifo_free();
	ret |= _wait_cmd_finish();

	_exit_nand_critical();

	return ret;
}

__s32 _read_in_page_mode_seq(NFC_CMD_LIST  *rcmd,void *mainbuf,void *sparebuf,__u8 dma_wait_mode)
{
	__s32 ret,ret1;
	__s32 i;
	__u32 cfg;
	NFC_CMD_LIST *cur_cmd,*read_addr_cmd;
	__u32 read_data_cmd,random_read_cmd0,random_read_cmd1;
	__u32 ecc_mode_temp;

	ret = 0;
	read_addr_cmd = rcmd;
	cur_cmd = rcmd;
	cur_cmd = cur_cmd->next;
	random_read_cmd0 = cur_cmd->value;
	cur_cmd = cur_cmd->next;
	random_read_cmd1 = cur_cmd->value;
	cur_cmd = cur_cmd->next;
	read_data_cmd = cur_cmd->value;

	//access NFC internal RAM by DMA bus
	NFC_WRITE_REG(NFC_REG_CTL, (NFC_READ_REG(NFC_REG_CTL)) | NFC_RAM_METHOD);

//	/*set dma and run*/
//	/*sdram*/
//	if (NFC_IS_SDRAM((__u32)mainbuf))
//		attr = 0x2810293;
//	/*sram*/
//	else
//		attr = 0x2800293;
	_dma_config_start(0, (__u32)mainbuf, pagesize);

	/*wait cmd fifo free*/
	ret = _wait_cmdfifo_free();
	if (ret)
		return ret;

	/*set NFC_REG_CNT*/
	NFC_WRITE_REG(NFC_REG_CNT,1024);

	/*set NFC_REG_RCMD_SET*/
	cfg = 0;
	cfg |= (read_data_cmd & 0xff);
	cfg |= ((random_read_cmd0 & 0xff) << 8);
	cfg |= ((random_read_cmd1 & 0xff) << 16);
	NFC_WRITE_REG(NFC_REG_RCMD_SET, cfg);

	/*set NFC_REG_SECTOR_NUM*/
	NFC_WRITE_REG(NFC_REG_SECTOR_NUM, pagesize/1024);

	/*set addr*/
	_set_addr(read_addr_cmd->addr,read_addr_cmd->addr_cycle);

	/*set NFC_REG_CMD*/
	cfg  = 0;
	cfg |= read_addr_cmd->value;
	/*set sequence mode*/
	cfg |= 0x1<<25;
	cfg |= ( (read_addr_cmd->addr_cycle - 1) << 16);
	cfg |= (NFC_SEND_ADR | NFC_DATA_TRANS | NFC_SEND_CMD1 | NFC_SEND_CMD2 | NFC_WAIT_FLAG | NFC_DATA_SWAP_METHOD);
	cfg |= ((__u32)0x2 << 30);//page command

	if (pagesize/1024 == 1)
		cfg |= NFC_SEQ;

	/*enable ecc*/
	_enable_ecc(1);
	
	/*set ecc to 24-bit ecc*/
    ecc_mode_temp = NFC_READ_REG(NFC_REG_ECC_CTL) & 0xf000;
	NFC_WRITE_REG(NFC_REG_ECC_CTL, ((NFC_READ_REG(NFC_REG_ECC_CTL) & (~NFC_ECC_MODE))|(0x1<<12)));

	NFC_WRITE_REG(NFC_REG_CMD,cfg);

    NAND_WaitDmaFinish();

	/*wait cmd fifo free and cmd finish*/
	ret = _wait_cmdfifo_free();
	ret |= _wait_cmd_finish();
	if (ret){
		_disable_ecc();
		return ret;
	}
	/*get user data*/
	for (i = 0; i < pagesize/1024;  i++){
		*(((__u32*) sparebuf)+i) = NFC_READ_REG(NFC_REG_USER_DATA(i));
	}

	/*ecc check and disable ecc*/
	ret = _check_ecc(pagesize/1024);
	_disable_ecc();

    /*set ecc to original value*/
	NFC_WRITE_REG(NFC_REG_ECC_CTL, (NFC_READ_REG(NFC_REG_ECC_CTL) & (~NFC_ECC_MODE))|ecc_mode_temp);
    
	/*if dma mode is wait*/
	if(0 == dma_wait_mode){
		ret1 = _wait_dma_end();
		if (ret1)
			return ret1;
	}

	return ret;
}


__s32 _read_in_page_mode_1K(NFC_CMD_LIST  *rcmd,void *mainbuf,void *sparebuf,__u8 dma_wait_mode)
{
	__s32 ret,ret1;
	__s32 i;
	__u32 cfg;
	NFC_CMD_LIST *cur_cmd,*read_addr_cmd;
	__u32 read_data_cmd,random_read_cmd0,random_read_cmd1;
	__u32 page_size_temp, ecc_mode_temp;

	ret = 0;
	read_addr_cmd = rcmd;
	cur_cmd = rcmd;
	cur_cmd = cur_cmd->next;
	random_read_cmd0 = cur_cmd->value;
	cur_cmd = cur_cmd->next;
	random_read_cmd1 = cur_cmd->value;
	cur_cmd = cur_cmd->next;
	read_data_cmd = cur_cmd->value;

	//access NFC internal RAM by DMA bus
	NFC_WRITE_REG(NFC_REG_CTL, (NFC_READ_REG(NFC_REG_CTL)) | NFC_RAM_METHOD);

	//set pagesize to 1K
      page_size_temp = (NFC_READ_REG(NFC_REG_CTL) & 0xf00)>>8;
	NFC_WRITE_REG(NFC_REG_CTL, (NFC_READ_REG(NFC_REG_CTL)) | (0x3<<8));

	///*set dma and run*/
	///*sdram*/
	//if (NFC_IS_SDRAM((__u32)mainbuf))
	//	attr = 0x2810293;
	///*sram*/
	//else
	//	attr = 0x2800293;
	_dma_config_start(0, (__u32)mainbuf, 1024);

	/*wait cmd fifo free*/
	ret = _wait_cmdfifo_free();
	if (ret)
		return ret;

	/*set NFC_REG_CNT*/
	NFC_WRITE_REG(NFC_REG_CNT,1024);

	/*set NFC_REG_RCMD_SET*/
	cfg = 0;
	cfg |= (read_data_cmd & 0xff);
	cfg |= ((random_read_cmd0 & 0xff) << 8);
	cfg |= ((random_read_cmd1 & 0xff) << 16);
	NFC_WRITE_REG(NFC_REG_RCMD_SET, cfg);

	/*set NFC_REG_SECTOR_NUM*/
	NFC_WRITE_REG(NFC_REG_SECTOR_NUM, 1024/1024);

	/*set addr*/
	_set_addr(read_addr_cmd->addr,read_addr_cmd->addr_cycle);

	/*set NFC_REG_CMD*/
	cfg  = 0;
	cfg |= read_addr_cmd->value;
	/*set sequence mode*/
	cfg |= 0x1<<25;
	cfg |= ( (read_addr_cmd->addr_cycle - 1) << 16);
	cfg |= (NFC_SEND_ADR | NFC_DATA_TRANS | NFC_SEND_CMD1 | NFC_SEND_CMD2 | NFC_WAIT_FLAG | NFC_DATA_SWAP_METHOD);
	cfg |= ((__u32)0x2 << 30);//page command

	if (1024/1024 == 1)
		cfg |= NFC_SEQ;

	/*enable ecc*/
	_enable_ecc(1);

	/*set ecc to 64-bit ecc*/
    ecc_mode_temp = NFC_READ_REG(NFC_REG_ECC_CTL) & 0xf000;
	NFC_WRITE_REG(NFC_REG_ECC_CTL, ((NFC_READ_REG(NFC_REG_ECC_CTL) & (~NFC_ECC_MODE))|(0x8<<12) ));

	NFC_WRITE_REG(NFC_REG_CMD,cfg);

    NAND_WaitDmaFinish();

	/*wait cmd fifo free and cmd finish*/
	ret = _wait_cmdfifo_free();
	ret |= _wait_cmd_finish();
	if (ret){
		_disable_ecc();
		return ret;
	}
	/*get user data*/
	for (i = 0; i < 1024/1024;  i++){
		*(((__u32*) sparebuf)+i) = NFC_READ_REG(NFC_REG_USER_DATA(i));
	}

	/*ecc check and disable ecc*/
	ret = _check_ecc(pagesize/1024);
	_disable_ecc();

	/*if dma mode is wait*/
	if(0 == dma_wait_mode){
		ret1 = _wait_dma_end();
		if (ret1)
			return ret1;
	}

	/*set ecc to original value*/
	NFC_WRITE_REG(NFC_REG_ECC_CTL, (NFC_READ_REG(NFC_REG_ECC_CTL) & (~NFC_ECC_MODE))|ecc_mode_temp);

    /*set pagesize to original value*/
    NFC_WRITE_REG(NFC_REG_CTL, ((NFC_READ_REG(NFC_REG_CTL)) & (~NFC_PAGE_SIZE)) | (page_size_temp<<8));

	return ret;
}


__s32 _read_in_page_mode_spare(NFC_CMD_LIST  *rcmd,void *mainbuf,void *sparebuf,__u8 dma_wait_mode)
{
	__s32 ret,ret1;
	__s32 i;
	__u32 cfg;
	NFC_CMD_LIST *cur_cmd,*read_addr_cmd;
	__u32 read_data_cmd,random_read_cmd0,random_read_cmd1;

	ret = 0;
	read_addr_cmd = rcmd;
	cur_cmd = rcmd;
	cur_cmd = cur_cmd->next;
	random_read_cmd0 = cur_cmd->value;
	cur_cmd = cur_cmd->next;
	random_read_cmd1 = cur_cmd->value;
	cur_cmd = cur_cmd->next;
	read_data_cmd = cur_cmd->value;

	//access NFC internal RAM by DMA bus
	NFC_WRITE_REG(NFC_REG_CTL, (NFC_READ_REG(NFC_REG_CTL)) | NFC_RAM_METHOD);


	///*set dma and run*/
	///*sdram*/
	//if (NFC_IS_SDRAM((__u32)mainbuf))
	//	attr = 0x2810293;
	///*sram*/
	//else
	//	attr = 0x2800293;
	_dma_config_start(0, (__u32)mainbuf, 2048);

	/*wait cmd fifo free*/
	ret = _wait_cmdfifo_free();
	if (ret)
		return ret;

	/*set NFC_REG_CNT*/
	NFC_WRITE_REG(NFC_REG_CNT,1024);

	/*set NFC_REG_RCMD_SET*/
	cfg = 0;
	cfg |= (read_data_cmd & 0xff);
	cfg |= ((random_read_cmd0 & 0xff) << 8);
	cfg |= ((random_read_cmd1 & 0xff) << 16);
	NFC_WRITE_REG(NFC_REG_RCMD_SET, cfg);

	/*set NFC_REG_SECTOR_NUM*/
	NFC_WRITE_REG(NFC_REG_SECTOR_NUM, 2048/1024);

	/*set addr*/
	_set_addr(read_addr_cmd->addr,read_addr_cmd->addr_cycle);

	/*set NFC_REG_CMD*/
	cfg  = 0;
	cfg |= read_addr_cmd->value;
	/*set sequence mode*/
	//cfg |= 0x1<<25;
	cfg |= ( (read_addr_cmd->addr_cycle - 1) << 16);
	cfg |= (NFC_SEND_ADR | NFC_DATA_TRANS | NFC_SEND_CMD1 | NFC_SEND_CMD2 | NFC_WAIT_FLAG | NFC_DATA_SWAP_METHOD);
	cfg |= ((__u32)0x2 << 30);//page command

	if (pagesize/1024 == 1)
		cfg |= NFC_SEQ;

	/*enable ecc*/
	_enable_ecc(1);
	NFC_WRITE_REG(NFC_REG_CMD,cfg);

    NAND_WaitDmaFinish();// 

	/*wait cmd fifo free and cmd finish*/
	ret = _wait_cmdfifo_free();
	ret |= _wait_cmd_finish();
	if (ret){
		_disable_ecc();
		return ret;
	}
	/*get user data*/
	for (i = 0; i < 2048/1024;  i++){
		*(((__u32*) sparebuf)+i) = NFC_READ_REG(NFC_REG_USER_DATA(i));
	}

	/*ecc check and disable ecc*/
	ret = _check_ecc(2048/1024);
	_disable_ecc();

	/*if dma mode is wait*/
	if(0 == dma_wait_mode){
		ret1 = _wait_dma_end();
		if (ret1)
			return ret1;
	}

	return ret;
}



__s32 NFC_Read_Seq(NFC_CMD_LIST  *rcmd, void *mainbuf, void *sparebuf, __u8 dma_wait_mode,__u8 page_mode )
{

	__s32 ret ;

	_enter_nand_critical();

	ret = _read_in_page_mode_seq(rcmd, mainbuf,sparebuf, dma_wait_mode);

	/*switch to ahb*/
	NFC_WRITE_REG(NFC_REG_CTL, (NFC_READ_REG(NFC_REG_CTL)) & (~NFC_RAM_METHOD));

	_exit_nand_critical();


	return ret;
}

__s32 NFC_Read_1K(NFC_CMD_LIST  *rcmd, void *mainbuf, void *sparebuf, __u8 dma_wait_mode,__u8 page_mode )
{

	__s32 ret ;

	_enter_nand_critical();

	ret = _read_in_page_mode_1K(rcmd, mainbuf,sparebuf, dma_wait_mode);

	/*switch to ahb*/
	NFC_WRITE_REG(NFC_REG_CTL, (NFC_READ_REG(NFC_REG_CTL)) & (~NFC_RAM_METHOD));

	_exit_nand_critical();


	return ret;
}

__s32 NFC_Read_Spare(NFC_CMD_LIST  *rcmd, void *mainbuf, void *sparebuf, __u8 dma_wait_mode,__u8 page_mode )
{

	__s32 ret ;

	_enter_nand_critical();

	ret = _read_in_page_mode_spare(rcmd, mainbuf,sparebuf, dma_wait_mode);

	/*switch to ahb*/
	NFC_WRITE_REG(NFC_REG_CTL, (NFC_READ_REG(NFC_REG_CTL)) & (~NFC_RAM_METHOD));

	_exit_nand_critical();


	return ret;
}

__s32 NFC_LSBInit(__u32 read_retry_type)
{
	//init
	read_retry_mode = (read_retry_type>>16)&0xff;
	read_retry_cycle =(read_retry_type>>8)&0xff;
	read_retry_reg_num = (read_retry_type>>0)&0xff;

	if(read_retry_mode == 1) //mode1
	{
		//set lsb mode
		lsb_mode_reg_num = 5;
		
		lsb_mode_reg_adr[0] = 0xa4;
		lsb_mode_reg_adr[1] = 0xa5;
		lsb_mode_reg_adr[2] = 0xb0;
		lsb_mode_reg_adr[3] = 0xb1;
		lsb_mode_reg_adr[4] = 0xc9;
		
		lsb_mode_val[0] = 0x25;
		lsb_mode_val[1] = 0x25;
		lsb_mode_val[2] = 0x25;
		lsb_mode_val[3] = 0x25;
		lsb_mode_val[4] = 0x1;
	}
	else
	{
	    return -1;
	}
	
	return 0;
}

__s32 LSB_GetDefaultParam(__u32 chip,__u8* default_value, __u32 read_retry_type)
{
    __s32 ret; 
    __u32 i; 
    
    ret =_vender_get_param(&lsb_mode_default_val[0], &lsb_mode_reg_adr[0], lsb_mode_reg_num);
    for(i=0; i<lsb_mode_reg_num; i++)
    {
        default_value[i] = lsb_mode_default_val[i];
    }

	return ret;
	
}

__s32 LSB_SetDefaultParam(__u32 chip,__u8* default_value, __u32 read_retry_type)
{
    __s32 ret; 
    
    ret =_vender_set_param(&lsb_mode_default_val[0], &lsb_mode_reg_adr[0], lsb_mode_reg_num);
    
	return ret;
}

__s32 NFC_LSBEnable(__u32 chip, __u32 read_retry_type)
{
    __u8 value[LSB_MODE_MAX_REG_NUM];
    __u32 i;
    
    //fix chip 0
    LSB_GetDefaultParam(0,value,read_retry_type);
    for(i=0;i<lsb_mode_reg_num;i++)
        value[i] += lsb_mode_val[i];
        
    _vender_set_param(value, &lsb_mode_reg_adr[0], lsb_mode_reg_num);
    
    return 0;
}

__s32 NFC_LSBDisable(__u32 chip, __u32 read_retry_type)
{
    __u8 value[LSB_MODE_MAX_REG_NUM];
        
    //fix chip 0
    LSB_SetDefaultParam(0,value, read_retry_type);
    
    return 0;
}

__s32 NFC_LSBExit(__u32 read_retry_type)
{

	return 0;
}

